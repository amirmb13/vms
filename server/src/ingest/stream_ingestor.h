#pragma once
// Dual-stream RTSP/ONVIF Profile T ingestion.
// Main: 4K@25fps (archive + fullscreen relay) | Sub: 360p@15fps (grid + motion)
// Packets are timestamped with the NTP-disciplined receive clock and pushed
// to attached sinks WITHOUT decoding (archiver) — decode happens only where
// strictly needed (motion sub-stream, AI shared-memory frames).

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

namespace vms {

class NtpClock;

struct HardwareCaps {
    bool gpu_available = false;
    bool has_avx2 = false;
    bool has_avx512 = false;
};

HardwareCaps detect_hardware_caps();

enum class StreamProfile : uint8_t { Main = 1, Mid = 2, Sub = 3 };

// A single ingested media packet + atomic-clock receive timestamp.
struct TimedPacket {
    AVPacket* pkt = nullptr;          // owned; ref-counted via av_packet_ref
    uint64_t utc_receive_us = 0;      // NTP-disciplined, microsecond precision
    StreamProfile profile;
    std::string camera_uuid;
};

// Sink interface implemented by RawArchiver, MediaRelayEngine, MotionEngine.
class IPacketSink {
public:
    virtual ~IPacketSink() = default;
    virtual void on_packet(const TimedPacket& packet) = 0;
};

struct CameraStreamConfig {
    std::string camera_uuid;
    std::string rtsp_url;
    StreamProfile profile;
    std::string codec;                // "h264" | "h265"
};

class StreamIngestor {
public:
    // Full config path: streams derived from vms.control.CameraConfigRequest
    // (Django is the single source of truth for RTSP endpoints/credentials).
    StreamIngestor(std::vector<CameraStreamConfig> streams, NtpClock& clock);

    // Convenience path used at registration time when only the camera UUID is
    // known yet: dual-stream (Main 4K + Sub 360p) endpoints are resolved from
    // the ONVIF Profile T media service before the reader threads start.
    StreamIngestor(const std::string& camera_uuid, NtpClock& clock);

    void attach_sink(IPacketSink& sink);
    void start();                     // spawns one reader thread per profile
    void stop();

    // Called by ControlService on Django "camera_config" / "stream_reset"
    // signals — tears down and re-opens RTSP sessions in-place, no restart.
    void apply_config_revision(uint64_t revision);

private:
    void reader_loop(const CameraStreamConfig& cfg);  // av_read_frame loop

    NtpClock& clock_;
    std::vector<CameraStreamConfig> streams_;
    std::vector<IPacketSink*> sinks_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> config_revision_{0};
};

}  // namespace vms
