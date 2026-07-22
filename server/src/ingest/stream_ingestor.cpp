// =============================================================================
// StreamIngestor — dual-stream RTSP/ONVIF Profile T ingestion.
//
// One reader thread per profile runs a tight av_read_frame() loop. Packets
// are stamped with the NTP-disciplined receive clock and fanned out to sinks
// (RawArchiver / MediaRelayEngine / MotionEngine) WITHOUT decoding — decode
// happens only where strictly needed downstream.
//
// Config changes from Django arrive as a revision bump via
// apply_config_revision(); reader loops notice the new revision, tear down
// their RTSP session in-place and re-open — no process restart, ever.
// =============================================================================
#include "ingest/stream_ingestor.h"

#include <chrono>
#include <cstdio>
#include <utility>

extern "C" {
#include <libavutil/opt.h>
}

#include "ntp/ntp_clock.h"

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

namespace vms {

// ---------------------------------------------------------------------------
// Hardware capability detection (hardware-agnostic execution mandate).
// ---------------------------------------------------------------------------
HardwareCaps detect_hardware_caps() {
    HardwareCaps caps;

#if defined(__GNUC__) || defined(__clang__)
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        caps.has_avx2 = (ebx & (1u << 5)) != 0;    // AVX2
        caps.has_avx512 = (ebx & (1u << 16)) != 0; // AVX-512F
    }
#elif defined(_MSC_VER)
    int regs[4] = {0};
    __cpuidex(regs, 7, 0);
    caps.has_avx2 = (regs[1] & (1 << 5)) != 0;
    caps.has_avx512 = (regs[1] & (1 << 16)) != 0;
#endif

#ifdef VMS_HAVE_CUDA
    // Probe without hard-failing on driverless hosts (CPU-only fallback).
    int device_count = 0;
    caps.gpu_available =
        (cudaGetDeviceCount(&device_count) == cudaSuccess) && device_count > 0;
#else
    caps.gpu_available = false;
#endif
    return caps;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
StreamIngestor::StreamIngestor(std::vector<CameraStreamConfig> streams,
                               NtpClock& clock)
    : clock_(clock), streams_(std::move(streams)) {}

StreamIngestor::StreamIngestor(const std::string& camera_uuid, NtpClock& clock)
    : clock_(clock) {
    // Registration-time scaffold: endpoints below are placeholders that the
    // first CameraConfigRequest revision replaces. Path convention matches
    // the ONVIF Profile T media profiles exposed by the discovery service.
    streams_.push_back({camera_uuid,
                        "rtsp://onvif-resolver.local/" + camera_uuid + "/main",
                        StreamProfile::Main, "h265"});
    streams_.push_back({camera_uuid,
                        "rtsp://onvif-resolver.local/" + camera_uuid + "/sub",
                        StreamProfile::Sub, "h264"});
}

void StreamIngestor::attach_sink(IPacketSink& sink) {
    sinks_.push_back(&sink);
}

void StreamIngestor::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    threads_.reserve(streams_.size());
    for (const auto& cfg : streams_) {
        threads_.emplace_back([this, cfg] { reader_loop(cfg); });
    }
}

void StreamIngestor::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void StreamIngestor::apply_config_revision(uint64_t revision) {
    // Reader loops compare against this after every packet; a bump makes them
    // close and re-open their RTSP session with the fresh configuration.
    config_revision_.store(revision, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Reader loop — one per (camera, profile)
// ---------------------------------------------------------------------------
void StreamIngestor::reader_loop(const CameraStreamConfig& cfg) {
    int backoff_ms = 500;  // exponential reconnect backoff, capped at 30 s

    while (running_.load(std::memory_order_acquire)) {
        const uint64_t session_revision =
            config_revision_.load(std::memory_order_acquire);

        // ---- open RTSP session (interleaved TCP: survives camera-VLAN NAT,
        //      no UDP packet loss on 4K main streams) ----------------------
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);      // 5 s socket timeout
        av_dict_set(&opts, "reorder_queue_size", "0", 0);  // TCP => in order
        av_dict_set(&opts, "max_delay", "500000", 0);

        AVFormatContext* fmt = avformat_alloc_context();
        // Non-blocking shutdown: FFmpeg polls this callback inside blocking IO.
        fmt->interrupt_callback.opaque = this;
        fmt->interrupt_callback.callback = [](void* opaque) -> int {
            auto* self = static_cast<StreamIngestor*>(opaque);
            return self->running_.load(std::memory_order_acquire) ? 0 : 1;
        };

        if (avformat_open_input(&fmt, cfg.rtsp_url.c_str(), nullptr, &opts) < 0 ||
            avformat_find_stream_info(fmt, nullptr) < 0) {
            av_dict_free(&opts);
            if (fmt) avformat_close_input(&fmt);
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 30'000);
            continue;
        }
        av_dict_free(&opts);
        backoff_ms = 500;  // healthy session — reset backoff

        const int video_index =
            av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        // ---- ingest loop: stamp + fan out, ZERO decode -------------------
        AVPacket* pkt = av_packet_alloc();
        while (running_.load(std::memory_order_acquire) &&
               config_revision_.load(std::memory_order_acquire) ==
                   session_revision) {
            const int rc = av_read_frame(fmt, pkt);
            if (rc < 0) break;  // EOF / network error -> reconnect

            if (pkt->stream_index == video_index) {
                TimedPacket timed;
                timed.pkt = pkt;
                timed.utc_receive_us = clock_.now_utc_us();  // atomic stamp
                timed.profile = cfg.profile;
                timed.camera_uuid = cfg.camera_uuid;

                // Sinks ref-count what they keep (ring buffers, archive
                // queue); ownership of `pkt` itself stays here.
                for (IPacketSink* sink : sinks_) sink->on_packet(timed);
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
        // Loop continues: either a config revision bump (fresh RTSP session
        // with the new endpoint/credentials) or a reconnect after failure.
    }
}

}  // namespace vms
