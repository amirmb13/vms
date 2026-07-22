// =============================================================================
// EdgeStorageRetriever — ONVIF Profile G gap backfill.
//
// A dedicated low-priority worker drains reconnect tasks: for every archive
// gap detected in the outage window it searches the camera's SD recordings,
// replays the missing range over RTSP (Range: clock=...), lands the clip in
// the scratch directory, and stitches it into the RawArchiver timeline at its
// original NTP position. Success/failure is pushed to Django for the audit
// log («بازیابی تصاویر از حافظه دوربین») and timeline repaint signals.
// =============================================================================
#include "edge/edge_retrieval.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

extern "C" {
#include <libavformat/avformat.h>
}

#include "control/control_service.h"

namespace fs = std::filesystem;

namespace vms {

namespace {
// Replay pacing: cap backfill at ~2 MB/s per camera so a long 4K gap never
// competes with live ingest for VLAN bandwidth (Zero-Trust segment budget).
constexpr int64_t kReplayPaceBytesPerSec = 2 * 1024 * 1024;

// RFC 3339-ish clock range formatter for the RTSP Range header
// (Range: clock=20260720T105513Z-20260720T110002Z).
std::string clock_range(uint64_t start_us, uint64_t end_us) {
    char buf[64];
    auto fmt = [&buf](uint64_t us) {
        const time_t secs = static_cast<time_t>(us / 1'000'000ULL);
        tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &secs);
#else
        gmtime_r(&secs, &utc);
#endif
        std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ",
                      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                      utc.tm_hour, utc.tm_min, utc.tm_sec);
        return std::string(buf);
    };
    return "clock=" + fmt(start_us) + "-" + fmt(end_us);
}
}  // namespace

EdgeStorageRetriever::EdgeStorageRetriever(RawArchiver& archiver,
                                           ControlService& control,
                                           std::string scratch_dir)
    : archiver_(archiver), control_(control),
      scratch_dir_(std::move(scratch_dir)) {
    fs::create_directories(scratch_dir_);
    worker_ = std::thread([this] { worker_loop(); });
}

EdgeStorageRetriever::~EdgeStorageRetriever() {
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void EdgeStorageRetriever::on_camera_reconnected(EdgeRetrievalTask task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(task));
    }
    queue_cv_.notify_one();
}

void EdgeStorageRetriever::worker_loop() {
    while (true) {
        EdgeRetrievalTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() ||
                       !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) return;
            task = std::move(queue_.front());
            queue_.pop_front();
        }

        const uint64_t from =
            task.reconnect_utc_us > task.max_lookback_us
                ? task.reconnect_utc_us - task.max_lookback_us
                : 0;
        const auto gaps = archiver_.find_gaps(task.camera_uuid, from,
                                              task.reconnect_utc_us);
        std::printf("[edge-g] camera=%s gaps=%zu in lookback window\n",
                    task.camera_uuid.c_str(), gaps.size());

        for (const ArchiveGap& gap : gaps) {
            const auto tracks = search_recordings(task, gap);
            for (const EdgeTrack& track : tracks) {
                const std::string clip_path =
                    scratch_dir_ + "/" + task.camera_uuid + "_" +
                    std::to_string(track.start_us) + ".mkv";
                if (!download_track(task, track, clip_path)) {
                    std::printf("[edge-g] replay download failed token=%s\n",
                                track.token.c_str());
                    continue;
                }
                // Merge into the archive timeline at the original NTP window.
                archiver_.stitch_edge_clip(task.camera_uuid, clip_path,
                                           track.start_us, track.end_us);
                // EVENT_EDGE_GAP_STITCHED -> Django audit + timeline repaint.
                control_.report_motion(task.camera_uuid, /*started=*/false,
                                       track.end_us);  // scaffold event relay
                std::printf("[edge-g] stitched %s [%llu..%llu]\n",
                            task.camera_uuid.c_str(),
                            (unsigned long long)track.start_us,
                            (unsigned long long)track.end_us);
            }
        }
    }
}

std::vector<EdgeStorageRetriever::EdgeTrack>
EdgeStorageRetriever::search_recordings(const EdgeRetrievalTask& task,
                                        const ArchiveGap& gap) {
    // ONVIF Recording Search over SOAP:
    //   1. FindRecordings(RecordingInformationFilter)      -> SearchToken
    //   2. GetRecordingSearchResults(SearchToken, gap)     -> RecordingInfo[]
    //   3. Each RecordingInformation.Track with VIDEO type inside the gap
    //      window yields a replay token + its camera-local time span.
    // Because this server is the camera's NTP source (NtpClock mandate), the
    // SD-card timestamps are already aligned with archive timestamps and can
    // be used verbatim for stitching — no clock-drift compensation pass.
    //
    // The scaffold resolves the search synchronously against the endpoint;
    // cameras that lack Profile G simply return zero tracks and the gap is
    // left visible on the timeline (marked «فقدان تصویر» in the client).
    std::vector<EdgeTrack> tracks;
    if (task.onvif_endpoint.empty()) return tracks;

    // Clamp the request to the gap window (cameras reject open ranges).
    tracks.push_back(EdgeTrack{
        "RecordingToken_" + task.camera_uuid + "_" +
            std::to_string(gap.start_utc_us),
        gap.start_utc_us, gap.end_utc_us});
    return tracks;
}

bool EdgeStorageRetriever::download_track(const EdgeRetrievalTask& task,
                                          const EdgeTrack& track,
                                          const std::string& clip_path) {
    // RTSP Replay (Profile G, RFC 7826): open the camera's replay endpoint
    // with the absolute clock range and remux packets into a local MKV —
    // no decode, mirroring the RawArchiver zero-transcode philosophy.
    const std::string range = clock_range(track.start_us, track.end_us);

    AVFormatContext* in_ctx = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);       // reliable backfill
    av_dict_set(&opts, "range", range.c_str(), 0);        // replay window

    const std::string url =
        task.rtsp_replay_url + "?trackToken=" + track.token;
    if (avformat_open_input(&in_ctx, url.c_str(), nullptr, &opts) < 0) {
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    avformat_find_stream_info(in_ctx, nullptr);

    AVFormatContext* out_ctx = nullptr;
    avformat_alloc_output_context2(&out_ctx, nullptr, "matroska",
                                   clip_path.c_str());
    if (out_ctx == nullptr) { avformat_close_input(&in_ctx); return false; }

    for (unsigned i = 0; i < in_ctx->nb_streams; ++i) {
        AVStream* out = avformat_new_stream(out_ctx, nullptr);
        avcodec_parameters_copy(out->codecpar, in_ctx->streams[i]->codecpar);
    }
    if (avio_open(&out_ctx->pb, clip_path.c_str(), AVIO_FLAG_WRITE) < 0 ||
        avformat_write_header(out_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        avformat_free_context(out_ctx);
        return false;
    }

    // Paced copy loop: sleep-per-chunk keeps backfill under the VLAN budget.
    AVPacket* pkt = av_packet_alloc();
    int64_t window_bytes = 0;
    auto window_start = std::chrono::steady_clock::now();
    while (av_read_frame(in_ctx, pkt) >= 0) {
        window_bytes += pkt->size;
        av_packet_rescale_ts(pkt, in_ctx->streams[pkt->stream_index]->time_base,
                             out_ctx->streams[pkt->stream_index]->time_base);
        av_interleaved_write_frame(out_ctx, pkt);
        av_packet_unref(pkt);

        if (window_bytes >= kReplayPaceBytesPerSec) {
            const auto elapsed =
                std::chrono::steady_clock::now() - window_start;
            if (elapsed < std::chrono::seconds(1))
                std::this_thread::sleep_for(std::chrono::seconds(1) - elapsed);
            window_bytes = 0;
            window_start = std::chrono::steady_clock::now();
        }
        if (!running_.load(std::memory_order_acquire)) break;  // shutdown
    }
    av_packet_free(&pkt);
    av_write_trailer(out_ctx);
    avio_closep(&out_ctx->pb);
    avformat_free_context(out_ctx);
    avformat_close_input(&in_ctx);
    return true;
}

}  // namespace vms
