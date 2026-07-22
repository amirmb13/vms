#pragma once
// Video Synopsis (TimeCompressor): background C++ threads scan a 24-hour
// archive window, extract moving-object "tubes" (a tracked object's boxes
// over time), then temporally compact all tubes onto a static background
// plate to produce a condensed short clip. Every rendered object carries its
// ORIGINAL NTP timestamp, so a click in the Qt client timeline jumps the
// archive player straight to the source moment (clickable timestamps).
//
// Hardware-agnostic mandate:
//   - GPU present  -> decode in VRAM, MOG2 on CUDA (shared with MotionEngine).
//   - CPU-only     -> keyframe-sampled decode + the same AVX2/AVX-512 SIMD
//                     3-frame difference engine used by MotionEngine Layer 3.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ingest/stream_ingestor.h"

namespace vms {

// Normalized bounding box observed at one sampled instant of a tube.
struct TubeSample {
    uint64_t utc_us = 0;      // original archive NTP timestamp
    float x = 0, y = 0;       // normalized top-left
    float w = 0, h = 0;       // normalized size
};

// One moving object tracked through the archive window.
struct ObjectTube {
    uint32_t tube_id = 0;
    uint64_t first_utc_us = 0;
    uint64_t last_utc_us = 0;
    std::vector<TubeSample> samples;
    // Assigned during temporal compaction: offset (us) of this tube's first
    // sample inside the condensed output clip.
    uint64_t synopsis_offset_us = 0;
};

struct SynopsisJob {
    std::string job_uuid;             // issued by Django (SynopsisJob row)
    std::string camera_uuid;
    uint64_t from_utc_us = 0;
    uint64_t to_utc_us = 0;           // typically from + 24h
    std::string output_dir;           // clip (.mp4) + manifest (.json) target
    uint32_t target_duration_s = 120; // condensed clip length budget
};

enum class SynopsisJobState : uint8_t {
    Queued = 0, Extracting = 1, Compacting = 2, Rendering = 3,
    Done = 4, Failed = 5,
};

// Progress callback -> ControlService pushes ServerEvent updates to Django
// so the client UI shows «در حال فشرده‌سازی ویدئو…» with a live percentage.
using SynopsisProgressFn =
    void (*)(void* ctx, const std::string& job_uuid,
             SynopsisJobState state, uint8_t percent);

class TimeCompressor final {
public:
    // storage_root must match RawArchiver's root: tubes are extracted by
    // replaying <root>/<camera>/<YYYYMMDD>/<HH>.vseg/.vidx segment pairs.
    TimeCompressor(std::string storage_root, const HardwareCaps& caps);
    ~TimeCompressor();

    // Called by ControlService on vms.control SYNOPSIS_REQUEST signals.
    void enqueue(SynopsisJob job);
    void cancel(const std::string& job_uuid);

    void set_progress_callback(SynopsisProgressFn fn, void* ctx);

private:
    void worker_loop();               // dedicated background thread pool of 1
                                      // (jobs are IO/CPU heavy; serialized so
                                      //  live ingest is never starved)

    // Stage 1 — tube extraction: keyframe-sampled decode of the archived
    // MAIN stream, downsampled to 320x240 GRAY8, SIMD 3-frame difference,
    // connected-component boxes, greedy IoU association into tubes.
    std::vector<ObjectTube> extract_tubes(const SynopsisJob& job);

    // Stage 2 — temporal compaction: greedy earliest-fit packing that maps
    // each tube's start into the condensed clip while keeping the spatial
    // collision energy between concurrently shown tubes under a threshold.
    void compact_tubes(std::vector<ObjectTube>& tubes,
                       uint64_t target_duration_us) const;

    // Stage 3 — render: median background plate + alpha-blended tube chips,
    // each stamped with its original Shamsi-formatted timestamp overlay;
    // encodes H.264 via libavcodec and writes the clickable-timestamp
    // manifest consumed by Django and the Qt timeline.
    bool render(const SynopsisJob& job, const std::vector<ObjectTube>& tubes);

    void report(const std::string& job_uuid, SynopsisJobState st, uint8_t pct);

    std::string storage_root_;
    HardwareCaps caps_;

    std::deque<SynopsisJob> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::string cancelled_job_;       // guarded by queue_mutex_
    std::thread worker_;
    std::atomic<bool> running_{true};

    SynopsisProgressFn progress_fn_ = nullptr;
    void* progress_ctx_ = nullptr;
};

}  // namespace vms
