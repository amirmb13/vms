#pragma once
// NTP-disciplined clock. Serves as the local NTP server for cameras/clients
// and stamps every ingested frame with an atomic Receive Timestamp (UTC, us)
// enabling millisecond-accurate multi-camera Sync Playback.
#include <atomic>
#include <cstdint>
#include <thread>

namespace vms {
class NtpClock {
public:
    NtpClock() = default;
    ~NtpClock();

    void start_local_server();        // UDP/123 responder thread
    void stop_local_server();
    uint64_t now_utc_us() const;      // disciplined monotonic-mapped UTC
    int64_t offset_us() const;        // current discipline offset

    // Applied by the upstream discipline loop (chrony/w32time sync or a
    // dedicated NTP client thread polling the site's stratum-1 source).
    void apply_discipline(int64_t measured_offset_us);

private:
    void server_loop();               // SNTP v4 responder

    std::atomic<int64_t> offset_us_{0};
    std::atomic<bool> running_{false};
    std::thread server_thread_;
};
}  // namespace vms
