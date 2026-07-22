// =============================================================================
// VMS Recording Server — process entry point.
//
// Boot sequence:
//   1. Detect hardware capabilities (GPU present? AVX2/AVX-512?)
//   2. Register with the Django Control Plane (ControlPlaneEvents.RegisterServer)
//      and receive the assigned camera set.
//   3. Start the NTP-disciplined clock (also serves local NTP).
//   4. Spawn one StreamIngestor per camera stream (main + sub, mid on demand).
//   5. Start the RawArchiver (async I/O direct packet writer).
//   6. Start the MediaRelayEngine (RTSP proxy / reflector + IGMP multicast).
//   7. Start the MotionEngine (3-layer hybrid detection).
//   8. Serve RecordingServerControl gRPC and subscribe to Redis control signals.
// =============================================================================
#include <atomic>
#include <csignal>
#include <cstdio>

#include "control/control_service.h"
#include "ingest/stream_ingestor.h"
#include "archiver/raw_archiver.h"
#include "relay/media_relay_engine.h"
#include "motion/motion_engine.h"
#include "ntp/ntp_clock.h"
#include "synopsis/time_compressor.h"
#include "edge/edge_retrieval.h"
#include "ipc/ai_inference_client.h"

static std::atomic<bool> g_running{true};

static void handle_signal(int) { g_running.store(false); }

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // --- 1. Hardware capability detection (hardware-agnostic mandate) -------
    vms::HardwareCaps caps = vms::detect_hardware_caps();
    std::printf("[vms] gpu=%d avx2=%d avx512=%d\n",
                caps.gpu_available, caps.has_avx2, caps.has_avx512);

    // --- 3. NTP-disciplined atomic clock -------------------------------------
    vms::NtpClock clock;
    clock.start_local_server();  // serves NTP to cameras & clients on the VLAN

    // --- 2 + 8. Control channel to/from Django -------------------------------
    vms::ControlService control{caps};
    auto assigned = control.register_with_control_plane();  // blocking retry loop

    // --- 5. Direct raw archiver (async epoll/IOCP packet writer) -------------
    vms::RawArchiver archiver{/*storage_root=*/"/mnt/vms-archive"};

    // --- 6. Media Relay: single ring buffer per stream, N subscribers --------
    vms::MediaRelayEngine relay{clock};

    // --- AI signaling channel (Docker Python container, zero-copy shm) -------
    //     Declared BEFORE MotionEngine on purpose: MotionEngine's destructor
    //     unregisters its ShmFrameWriter rings against this client, so the
    //     client must outlive the engine (reverse destruction order).
    vms::AiInferenceClient ai_client;  // default localhost:50061

    // --- 7. 3-layer hybrid motion detection ----------------------------------
    vms::MotionEngine motion{caps, control};

    // --- Video Synopsis (TimeCompressor): 24h archive -> condensed clip with
    //     clickable original timestamps. Jobs arrive via ControlService
    //     (SYNOPSIS_REQUEST control signals pushed from Django).
    vms::TimeCompressor synopsis{/*storage_root=*/"/mnt/vms-archive", caps};

    // --- Edge Storage Retrieval (ONVIF Profile G): backfills archive gaps
    //     from camera SD cards after reconnection, stitched into the timeline.
    vms::EdgeStorageRetriever edge_retrieval{archiver, control,
                                             /*scratch_dir=*/"/mnt/vms-archive/.edge-scratch"};

    // --- AI detections relay: stream back on a dedicated reader thread and
    //     forward to Django as EVENT_AI_FACE_MATCH ServerEvents (forensic
    //     ingest).
    ai_client.set_result_callback([&control](const vms::ai::InferenceResult& r) {
        std::string payload = "{\"backend\":\"" + r.backend() + "\",\"faces\":[";
        for (int i = 0; i < r.faces_size(); ++i) {
            const auto& f = r.faces(i);
            if (i > 0) payload += ',';
            payload += "{\"identity\":\"" + f.identity_uuid() +
                       "\",\"similarity\":" + std::to_string(f.similarity()) + "}";
        }
        payload += "],\"objects\":[";
        for (int i = 0; i < r.objects_size(); ++i) {
            const auto& o = r.objects(i);
            if (i > 0) payload += ',';
            payload += "{\"label\":\"" + o.label() + "\",\"attributes\":{";
            bool first = true;
            for (const auto& [k, v] : o.attributes()) {
                if (!first) payload += ',';
                payload += "\"" + k + "\":\"" + v + "\"";
                first = false;
            }
            payload += "}}";
        }
        payload += "]}";
        control.report_event(r.camera_uuid(), /*EVENT_AI_FACE_MATCH=*/7,
                             r.utc_epoch_ms(), payload);
    });
    if (ai_client.start()) {
        // Zero-copy AI path: MotionEngine owns the only sub-stream decoders,
        // so it hosts the per-camera ShmFrameWriter rings and announces
        // decoded frames — motion-gated — to the Python container. Until
        // CameraConfigRequest grows a per-camera AI flag, every assigned
        // camera is armed; idle cameras cost nothing (announce requires
        // active motion).
        motion.set_ai_client(&ai_client);
        for (const auto& cam : assigned.cameras())
            motion.set_ai_enabled(cam, true);
    } else {
        // AI container absent is a legal deployment (recording-only node);
        // the supervisor retries start() when Django enables AI for a camera.
        std::puts("[vms] AI engine unavailable — continuing without inference");
    }

    // --- 4. One ingestor per assigned camera ---------------------------------
    std::vector<std::unique_ptr<vms::StreamIngestor>> ingestors;
    for (const auto& cam : assigned.cameras()) {
        auto ing = std::make_unique<vms::StreamIngestor>(cam, clock);
        ing->attach_sink(archiver);   // main stream -> raw packets to disk
        ing->attach_sink(relay);      // all streams -> relay ring buffers
        ing->attach_sink(motion);     // sub stream  -> motion pipeline
        ing->start();
        ingestors.push_back(std::move(ing));
    }

    control.serve_blocking(g_running);  // gRPC server + Redis subscriber loop

    for (auto& ing : ingestors) ing->stop();
    ai_client.stop();  // half-close the bidi stream, join the reader thread
    std::puts("[vms] shutdown complete");
    return 0;
}
