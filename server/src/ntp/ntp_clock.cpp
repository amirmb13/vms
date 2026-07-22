// =============================================================================
// NtpClock — NTP-disciplined atomic clock + local SNTP v4 server (UDP/123).
//
// Every ingested media packet is stamped with now_utc_us() the instant it
// leaves av_read_frame(). Because ALL recording servers, cameras, and clients
// discipline against the same source, a master timeline timestamp broadcast
// during Global Sync Playback lands on the same real-world instant across
// every camera in the enterprise (millisecond accuracy).
// =============================================================================
#include "ntp/ntp_clock.h"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace vms {

namespace {

// Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
constexpr uint64_t kNtpUnixDeltaSec = 2'208'988'800ULL;

// 48-byte SNTP v4 packet layout (RFC 4330).
struct NtpPacket {
    uint8_t li_vn_mode;        // leap indicator | version | mode
    uint8_t stratum;
    uint8_t poll;
    int8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;
    uint32_t ref_ts_sec, ref_ts_frac;
    uint32_t orig_ts_sec, orig_ts_frac;
    uint32_t recv_ts_sec, recv_ts_frac;
    uint32_t tx_ts_sec, tx_ts_frac;
};
static_assert(sizeof(NtpPacket) == 48, "SNTP packet must be 48 bytes");

inline void utc_us_to_ntp(uint64_t utc_us, uint32_t& sec, uint32_t& frac) {
    const uint64_t s = utc_us / 1'000'000ULL + kNtpUnixDeltaSec;
    const uint64_t us = utc_us % 1'000'000ULL;
    sec = htonl(static_cast<uint32_t>(s));
    // microseconds -> 32-bit binary fraction of a second
    frac = htonl(static_cast<uint32_t>((us << 32) / 1'000'000ULL));
}

}  // namespace

NtpClock::~NtpClock() { stop_local_server(); }

uint64_t NtpClock::now_utc_us() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const int64_t raw_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return static_cast<uint64_t>(raw_us +
                                 offset_us_.load(std::memory_order_relaxed));
}

int64_t NtpClock::offset_us() const {
    return offset_us_.load(std::memory_order_relaxed);
}

void NtpClock::apply_discipline(int64_t measured_offset_us) {
    // Slew, don't step: converge 1/8th of the measured error per poll so the
    // archive timeline never jumps backwards mid-recording.
    const int64_t current = offset_us_.load(std::memory_order_relaxed);
    offset_us_.store(current + (measured_offset_us - current) / 8,
                     std::memory_order_relaxed);
}

void NtpClock::start_local_server() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    server_thread_ = std::thread([this] { server_loop(); });
}

void NtpClock::stop_local_server() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    if (server_thread_.joinable()) server_thread_.join();
}

void NtpClock::server_loop() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const int fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (fd < 0) { running_.store(false); return; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(123);  // needs CAP_NET_BIND_SERVICE / service acct
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        // Fall back to the unprivileged mirror port; the deployment firewall
        // DNATs 123 -> 10123 on the camera VLAN.
        addr.sin_port = htons(10123);
        ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    // 250 ms receive timeout so the loop notices running_ == false promptly.
#ifdef _WIN32
    DWORD tv = 250;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{0, 250'000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    NtpPacket req{};
    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in peer{};
        socklen_type peer_len = sizeof(peer);
        const auto n = ::recvfrom(fd, reinterpret_cast<char*>(&req),
                                  sizeof(req), 0,
                                  reinterpret_cast<sockaddr*>(&peer),
                                  &peer_len);
        if (n < static_cast<decltype(n)>(sizeof(NtpPacket))) continue;

        const uint64_t rx_us = now_utc_us();  // receive timestamp — ASAP

        NtpPacket resp{};
        resp.li_vn_mode = (0 << 6) | (4 << 3) | 4;  // no-leap | v4 | server
        resp.stratum = 2;                           // disciplined secondary
        resp.poll = req.poll;
        resp.precision = -20;                       // ~1 microsecond
        resp.reference_id = htonl(0x564D5300);      // "VMS\0"
        // Echo client's transmit ts into "originate" (round-trip calc).
        resp.orig_ts_sec = req.tx_ts_sec;
        resp.orig_ts_frac = req.tx_ts_frac;
        utc_us_to_ntp(rx_us, resp.recv_ts_sec, resp.recv_ts_frac);
        utc_us_to_ntp(rx_us, resp.ref_ts_sec, resp.ref_ts_frac);
        utc_us_to_ntp(now_utc_us(), resp.tx_ts_sec, resp.tx_ts_frac);

        ::sendto(fd, reinterpret_cast<const char*>(&resp), sizeof(resp), 0,
                 reinterpret_cast<const sockaddr*>(&peer), peer_len);
    }

#ifdef _WIN32
    ::closesocket(fd);
    WSACleanup();
#else
    ::close(fd);
#endif
}

}  // namespace vms
