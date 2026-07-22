#pragma once
// Control channel with the Django Control Plane:
//  - serves vms.control.RecordingServerControl (gRPC)
//  - subscribes to Redis channel "vms:control-signals"
//  - pushes ServerEvent streams (motion, offline, AI matches) to Django
//    via ControlPlaneEvents.ReportEvent (client streaming, auto-reconnect)
//
// Environment:
//   VMS_SERVER_UUID          stable node identity (generated if absent)
//   VMS_CONTROL_PLANE_GRPC   Django ControlPlaneEvents endpoint
//                            (default: localhost:50060)
//   VMS_CONTROL_LISTEN_PORT  RecordingServerControl listen port (def: 50051)
//   VMS_REDIS_HOST/PORT      control-signal Pub/Sub (default: localhost:6379)
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ingest/stream_ingestor.h"

namespace vms {

class ControlService {
public:
    explicit ControlService(const HardwareCaps& caps);
    ~ControlService();

    ControlService(const ControlService&) = delete;
    ControlService& operator=(const ControlService&) = delete;

    // Assigned camera set returned by Django on registration. Full stream
    // configs are exposed so main.cpp can build StreamIngestors directly.
    struct AssignedCameras {
        std::vector<std::string> camera_uuids;
        std::vector<std::vector<CameraStreamConfig>> stream_configs;  // per camera
        const std::vector<std::string>& cameras() const { return camera_uuids; }
    };

    // Blocking-retry registration against ControlPlaneEvents.RegisterServer.
    AssignedCameras register_with_control_plane();

    // Serves RecordingServerControl + Redis subscriber until running == false.
    void serve_blocking(std::atomic<bool>& running);

    // Queue a motion event onto the ReportEvent stream (non-blocking).
    void report_motion(const std::string& camera_uuid, bool started, uint64_t utc_us);

    // Generic event relay (camera online/offline, storage, AI face match…).
    // `type` matches vms.control.EventType numeric values.
    void report_event(const std::string& camera_uuid, int type,
                      uint64_t utc_ms, const std::string& payload_json);

    // --- Callbacks wired by main.cpp so control signals reach the pipeline --
    using CameraConfigHandler =
        std::function<void(const std::string& camera_uuid, uint64_t revision)>;
    using StreamResetHandler =
        std::function<void(const std::string& camera_uuid, int profile)>;
    using MotionRoiHandler =
        std::function<void(const std::string& camera_uuid,
                           const std::vector<uint8_t>& bitmask,
                           uint32_t cols, uint32_t rows, uint32_t sensitivity)>;
    using PruneHandler =
        std::function<bool(const std::string& camera_uuid, uint64_t before_utc_ms,
                           const std::string& sig_a, const std::string& sig_b)>;

    void on_camera_config(CameraConfigHandler cb);
    void on_stream_reset(StreamResetHandler cb);
    void on_motion_roi(MotionRoiHandler cb);
    void on_prune(PruneHandler cb);

    const std::string& server_uuid() const;

private:
    HardwareCaps caps_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vms
