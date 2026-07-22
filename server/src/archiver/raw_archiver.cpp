// =============================================================================
// RawArchiver — Direct Raw Archiving to SAN/NAS.
//
// Design: media packets are written UNDECODED. The ingest thread only copies
// the compressed payload into a lock-guarded queue (~a few hundred KB/s per
// camera); a single asynchronous writer thread drains the queue with large
// sequential appends — the access pattern SAN/NAS firmware optimizes for.
// CPU cost during storage is effectively zero because no codec ever runs.
//
// Layout on disk (per camera, per hour):
//   <root>/<camera_uuid>/<YYYYMMDD>/<HH>.vseg   raw packet payloads
//   <root>/<camera_uuid>/<YYYYMMDD>/<HH>.vidx   16-byte IndexEntry records
//
// The .vidx timestamp index is what powers millisecond Sync Playback seeks,
// gap detection for ONVIF Profile G retrieval, and timeline rendering.
// =============================================================================
#include "archiver/raw_archiver.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vms {

namespace {
// A hole longer than this in the packet index is treated as a disconnection
// window that Edge Storage Retrieval (Profile G) should try to backfill.
constexpr uint64_t kGapThresholdUs = 5'000'000;  // 5 seconds
}  // namespace

RawArchiver::RawArchiver(std::string storage_root)
    : storage_root_(std::move(storage_root)) {
    fs::create_directories(storage_root_);
    writer_thread_ = std::thread([this] { writer_loop(); });
}

RawArchiver::~RawArchiver() {
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) writer_thread_.join();
}

void RawArchiver::on_packet(const TimedPacket& packet) {
    // Archive policy: MAIN stream only — sub/mid streams exist for grid
    // rendering and motion detection, not for evidence storage.
    if (packet.profile != StreamProfile::Main || packet.pkt == nullptr) return;

    QueuedWrite w;
    w.camera_uuid = packet.camera_uuid;
    w.utc_us = packet.utc_receive_us;
    w.keyframe = (packet.pkt->flags & AV_PKT_FLAG_KEY) != 0;
    w.payload.assign(packet.pkt->data, packet.pkt->data + packet.pkt->size);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(w));
    }
    queue_cv_.notify_one();
}

std::string RawArchiver::segment_dir(const std::string& camera_uuid,
                                     uint64_t utc_us) const {
    const time_t secs = static_cast<time_t>(utc_us / 1'000'000ULL);
    tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &secs);
#else
    gmtime_r(&secs, &utc);
#endif
    char day[16];
    std::snprintf(day, sizeof(day), "%04d%02d%02d", utc.tm_year + 1900,
                  utc.tm_mon + 1, utc.tm_mday);
    return storage_root_ + "/" + camera_uuid + "/" + day;
}

void RawArchiver::writer_loop() {
    // NOTE: production Linux builds submit these appends through io_uring
    // (IOCP on Windows) with O_DIRECT-aligned buffers; std::ofstream keeps
    // the scaffold portable while preserving the identical queue contract.
    std::deque<QueuedWrite> batch;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() ||
                       !running_.load(std::memory_order_acquire);
            });
            if (queue_.empty() && !running_.load(std::memory_order_acquire))
                return;  // drained + shutting down
            batch.swap(queue_);  // take everything in one lock acquisition
        }

        for (QueuedWrite& w : batch) {
            const time_t secs = static_cast<time_t>(w.utc_us / 1'000'000ULL);
            tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &secs);
#else
            gmtime_r(&secs, &utc);
#endif
            char hour[8];
            std::snprintf(hour, sizeof(hour), "%02d", utc.tm_hour);

            const std::string dir = segment_dir(w.camera_uuid, w.utc_us);
            fs::create_directories(dir);
            const std::string seg_path = dir + "/" + hour + ".vseg";
            const std::string idx_path = dir + "/" + hour + ".vidx";

            std::ofstream seg(seg_path, std::ios::binary | std::ios::app);
            const uint32_t offset = static_cast<uint32_t>(seg.tellp());
            seg.write(reinterpret_cast<const char*>(w.payload.data()),
                      static_cast<std::streamsize>(w.payload.size()));

            IndexEntry entry{w.utc_us, offset,
                             (static_cast<uint32_t>(w.payload.size()) << 1) |
                                 (w.keyframe ? 1u : 0u)};
            std::ofstream idx(idx_path, std::ios::binary | std::ios::app);
            idx.write(reinterpret_cast<const char*>(&entry), sizeof(entry));

            std::lock_guard<std::mutex> lock(index_mutex_);
            index_[w.camera_uuid].push_back(entry);
        }
        batch.clear();
    }
}

std::vector<ArchiveGap> RawArchiver::find_gaps(const std::string& camera_uuid,
                                               uint64_t from_utc_us,
                                               uint64_t to_utc_us) {
    std::vector<ArchiveGap> gaps;
    std::lock_guard<std::mutex> lock(index_mutex_);
    const auto it = index_.find(camera_uuid);
    if (it == index_.end() || it->second.empty()) {
        gaps.push_back({camera_uuid, from_utc_us, to_utc_us});
        return gaps;
    }

    // Index entries are appended in receive order — already sorted by time.
    uint64_t previous = from_utc_us;
    for (const IndexEntry& e : it->second) {
        if (e.utc_us < from_utc_us) { previous = std::max(previous, e.utc_us); continue; }
        if (e.utc_us > to_utc_us) break;
        if (e.utc_us - previous > kGapThresholdUs) {
            gaps.push_back({camera_uuid, previous, e.utc_us});
        }
        previous = e.utc_us;
    }
    if (to_utc_us > previous && to_utc_us - previous > kGapThresholdUs) {
        gaps.push_back({camera_uuid, previous, to_utc_us});
    }
    return gaps;
}

void RawArchiver::stitch_edge_clip(const std::string& camera_uuid,
                                   const std::string& clip_path,
                                   uint64_t start_utc_us,
                                   uint64_t end_utc_us) {
    // ONVIF Profile G: the clip downloaded from the camera's SD card is
    // parked next to the segment covering the gap window. Timeline queries
    // union the .vidx index with .stitch manifests, so the recovered footage
    // appears seamlessly at its original NTP position — no re-muxing.
    const std::string dir = segment_dir(camera_uuid, start_utc_us);
    fs::create_directories(dir);

    const std::string manifest_path =
        dir + "/" + std::to_string(start_utc_us) + ".stitch";
    std::ofstream manifest(manifest_path, std::ios::trunc);
    manifest << "clip=" << clip_path << "\n"
             << "start_utc_us=" << start_utc_us << "\n"
             << "end_utc_us=" << end_utc_us << "\n";

    // Mark the window as covered so find_gaps() stops re-requesting it.
    std::lock_guard<std::mutex> lock(index_mutex_);
    auto& entries = index_[camera_uuid];
    entries.push_back({start_utc_us, 0, 0});
    entries.push_back({end_utc_us, 0, 0});
    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.utc_us < b.utc_us;
              });
}

}  // namespace vms
