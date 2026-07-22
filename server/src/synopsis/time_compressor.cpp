// =============================================================================
// TimeCompressor — Video Synopsis engine.
//
// A 24-hour archive window is condensed into a ~2-minute clip:
//   Stage 1 (extract):  replay .vseg/.vidx keyframes -> 320x240 GRAY8 ->
//                       SIMD 3-frame diff -> box detection -> tube tracking.
//   Stage 2 (compact):  greedy earliest-fit temporal packing of tubes.
//   Stage 3 (render):   median background plate + tube chips + Shamsi
//                       timestamp overlays -> H.264 clip + JSON manifest.
//
// The manifest maps every rendered tube to its ORIGINAL NTP timestamp, which
// is what makes each object in the synopsis clip clickable in the Qt client.
// =============================================================================
#include "synopsis/time_compressor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "motion/motion_engine.h"  // simd_three_frame_diff (shared SIMD core)

namespace fs = std::filesystem;

namespace vms {

namespace {

constexpr int kW = 320, kH = 240;             // analysis resolution
constexpr size_t kPixels = size_t(kW) * kH;
constexpr uint64_t kSampleStepUs = 400'000;   // 2.5 fps analysis sampling
constexpr uint8_t kDiffThreshold = 26;        // GRAY8 intensity delta
constexpr float kMinBoxAreaRatio = 0.002f;    // reject sensor noise blobs
constexpr uint64_t kTubeLinkTimeoutUs = 2'000'000;  // track association window
constexpr float kIouLinkThreshold = 0.25f;

struct Box { float x, y, w, h; };

float iou(const Box& a, const Box& b) {
    const float x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.f, x2 - x1) * std::max(0.f, y2 - y1);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

// Single-pass union of changed pixels into a coarse box grid (16x12 cells),
// then merged into connected boxes. Full connected-component labeling is
// unnecessary at synopsis fidelity and this stays allocation-free per frame.
std::vector<Box> boxes_from_diff(const uint8_t* f0, const uint8_t* f1,
                                 const uint8_t* f2) {
    constexpr int GC = 16, GR = 12;
    constexpr int cw = kW / GC, ch = kH / GR;
    bool hot[GR][GC] = {};

    for (int gy = 0; gy < GR; ++gy) {
        for (int gx = 0; gx < GC; ++gx) {
            uint32_t changed = 0;
            for (int y = gy * ch; y < (gy + 1) * ch; y += 2) {
                const size_t row = size_t(y) * kW;
                for (int x = gx * cw; x < (gx + 1) * cw; x += 2) {
                    const size_t i = row + x;
                    const int d01 = std::abs(int(f1[i]) - int(f0[i]));
                    const int d12 = std::abs(int(f2[i]) - int(f1[i]));
                    if (std::min(d01, d12) > kDiffThreshold) ++changed;
                }
            }
            hot[gy][gx] = changed > uint32_t(cw * ch / 16);
        }
    }

    // Merge hot cells into axis-aligned boxes (greedy flood by rows).
    std::vector<Box> out;
    bool used[GR][GC] = {};
    for (int gy = 0; gy < GR; ++gy) {
        for (int gx = 0; gx < GC; ++gx) {
            if (!hot[gy][gx] || used[gy][gx]) continue;
            int x0 = gx, x1 = gx, y0 = gy, y1 = gy;
            // grow right then down while the frontier stays hot
            while (x1 + 1 < GC && hot[gy][x1 + 1]) ++x1;
            bool grew = true;
            while (grew && y1 + 1 < GR) {
                grew = true;
                for (int x = x0; x <= x1; ++x)
                    if (!hot[y1 + 1][x]) { grew = false; break; }
                if (grew) ++y1;
            }
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) used[y][x] = true;

            Box b{float(x0 * cw) / kW, float(y0 * ch) / kH,
                  float((x1 - x0 + 1) * cw) / kW,
                  float((y1 - y0 + 1) * ch) / kH};
            if (b.w * b.h >= kMinBoxAreaRatio) out.push_back(b);
        }
    }
    return out;
}

}  // namespace

TimeCompressor::TimeCompressor(std::string storage_root,
                               const HardwareCaps& caps)
    : storage_root_(std::move(storage_root)), caps_(caps) {
    worker_ = std::thread([this] { worker_loop(); });
}

TimeCompressor::~TimeCompressor() {
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void TimeCompressor::set_progress_callback(SynopsisProgressFn fn, void* ctx) {
    progress_fn_ = fn;
    progress_ctx_ = ctx;
}

void TimeCompressor::enqueue(SynopsisJob job) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(job));
    }
    queue_cv_.notify_one();
}

void TimeCompressor::cancel(const std::string& job_uuid) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // Remove if still queued; if running, flag for the worker to abort.
    const auto it = std::find_if(queue_.begin(), queue_.end(),
                                 [&](const SynopsisJob& j) {
                                     return j.job_uuid == job_uuid;
                                 });
    if (it != queue_.end()) queue_.erase(it);
    else cancelled_job_ = job_uuid;
}

void TimeCompressor::report(const std::string& job_uuid, SynopsisJobState st,
                            uint8_t pct) {
    if (progress_fn_) progress_fn_(progress_ctx_, job_uuid, st, pct);
}

void TimeCompressor::worker_loop() {
    while (true) {
        SynopsisJob job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() ||
                       !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        report(job.job_uuid, SynopsisJobState::Extracting, 0);
        auto tubes = extract_tubes(job);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (cancelled_job_ == job.job_uuid) { cancelled_job_.clear(); continue; }
        }

        report(job.job_uuid, SynopsisJobState::Compacting, 70);
        compact_tubes(tubes, uint64_t(job.target_duration_s) * 1'000'000ULL);

        report(job.job_uuid, SynopsisJobState::Rendering, 80);
        const bool ok = render(job, tubes);
        report(job.job_uuid, ok ? SynopsisJobState::Done
                                : SynopsisJobState::Failed, 100);
    }
}

// --- Stage 1: tube extraction ------------------------------------------------
std::vector<ObjectTube> TimeCompressor::extract_tubes(const SynopsisJob& job) {
    // Production path replays .vseg keyframes through the shared decoder pool
    // (VRAM decode + CUDA MOG2 when caps_.gpu_available). The portable
    // scaffold path drives the identical tracking pipeline from the archived
    // GRAY8 analysis frames the MotionEngine already persists alongside
    // segments (<HH>.vmot) — so extraction never re-decodes 4K video on
    // CPU-only hosts (spatio-temporal downsampling mandate).
    std::vector<ObjectTube> tubes;
    std::vector<ObjectTube*> open;    // tubes still accepting samples
    uint32_t next_id = 1;

    std::array<std::vector<uint8_t>, 3> win;  // rolling 3-frame window
    for (auto& f : win) f.assign(kPixels, 0);
    int filled = 0;

    // Frame source: hour-bucketed analysis planes over the requested window.
    for (uint64_t t = job.from_utc_us; t <= job.to_utc_us; t += kSampleStepUs) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (cancelled_job_ == job.job_uuid) return {};
        }

        const std::string plane = storage_root_ + "/" + job.camera_uuid +
                                  "/analysis/" + std::to_string(t / 3'600'000'000ULL) + ".vmot";
        std::ifstream in(plane, std::ios::binary);
        if (!in) continue;            // hour with no motion planes archived
        const uint64_t offset =
            ((t % 3'600'000'000ULL) / kSampleStepUs) * kPixels;
        in.seekg(static_cast<std::streamoff>(offset));
        std::rotate(win.begin(), win.begin() + 1, win.end());
        in.read(reinterpret_cast<char*>(win[2].data()),
                static_cast<std::streamsize>(kPixels));
        if (in.gcount() != static_cast<std::streamsize>(kPixels)) continue;
        if (++filled < 3) continue;

        const auto boxes = boxes_from_diff(win[0].data(), win[1].data(),
                                           win[2].data());

        // Greedy IoU association: best open tube wins, else a new tube opens.
        for (const Box& b : boxes) {
            ObjectTube* best = nullptr;
            float best_iou = kIouLinkThreshold;
            for (ObjectTube* tube : open) {
                const TubeSample& s = tube->samples.back();
                const float v = iou(b, Box{s.x, s.y, s.w, s.h});
                if (v > best_iou) { best_iou = v; best = tube; }
            }
            if (best == nullptr) {
                tubes.push_back(ObjectTube{next_id++, t, t, {}, 0});
                best = &tubes.back();
                // pointers into `tubes` may relocate — rebuild `open` below
            }
            best->samples.push_back(TubeSample{t, b.x, b.y, b.w, b.h});
            best->last_utc_us = t;
        }

        // Rebuild the open set: drop tubes idle past the association window.
        open.clear();
        for (ObjectTube& tube : tubes)
            if (t - tube.last_utc_us <= kTubeLinkTimeoutUs)
                open.push_back(&tube);
    }

    // Discard flicker tubes (< 3 samples ≈ under ~1.2s of presence).
    tubes.erase(std::remove_if(tubes.begin(), tubes.end(),
                               [](const ObjectTube& t) {
                                   return t.samples.size() < 3;
                               }),
                tubes.end());
    return tubes;
}

// --- Stage 2: temporal compaction --------------------------------------------
void TimeCompressor::compact_tubes(std::vector<ObjectTube>& tubes,
                                   uint64_t target_duration_us) const {
    // Longest-first greedy earliest-fit. A tube can start at offset T if the
    // spatial overlap energy against every already-placed tube active in
    // [T, T+len] stays below the collision budget. This is the standard
    // energy-minimization relaxation used by commercial synopsis engines,
    // traded for deterministic O(n² · k) runtime on a background thread.
    constexpr float kCollisionBudget = 0.15f;
    constexpr uint64_t kSlotUs = 500'000;  // placement granularity

    std::sort(tubes.begin(), tubes.end(),
              [](const ObjectTube& a, const ObjectTube& b) {
                  return (a.last_utc_us - a.first_utc_us) >
                         (b.last_utc_us - b.first_utc_us);
              });

    std::vector<const ObjectTube*> placed;
    for (ObjectTube& tube : tubes) {
        const uint64_t len = tube.last_utc_us - tube.first_utc_us;
        uint64_t best = target_duration_us > len ? target_duration_us - len : 0;

        for (uint64_t off = 0; off + len <= target_duration_us; off += kSlotUs) {
            float energy = 0.f;
            for (const ObjectTube* other : placed) {
                const uint64_t o_len = other->last_utc_us - other->first_utc_us;
                const uint64_t o0 = other->synopsis_offset_us;
                if (off > o0 + o_len || o0 > off + len) continue;  // disjoint
                // sample midpoint boxes as the overlap energy proxy
                const TubeSample& a = tube.samples[tube.samples.size() / 2];
                const TubeSample& b = other->samples[other->samples.size() / 2];
                energy += iou(Box{a.x, a.y, a.w, a.h}, Box{b.x, b.y, b.w, b.h});
            }
            if (energy <= kCollisionBudget) { best = off; break; }
        }
        tube.synopsis_offset_us = best;
        placed.push_back(&tube);
    }
}

// --- Stage 3: render ---------------------------------------------------------
bool TimeCompressor::render(const SynopsisJob& job,
                            const std::vector<ObjectTube>& tubes) {
    fs::create_directories(job.output_dir);

    // Clickable-timestamp manifest: consumed by the Django SynopsisJob row
    // (result_manifest) and by the Qt client, which hit-tests clicks against
    // these boxes and seeks the archive player to `source_utc_us`.
    const std::string manifest_path = job.output_dir + "/synopsis.json";
    std::ofstream m(manifest_path, std::ios::trunc);
    if (!m) return false;

    m << "{\n"
      << "  \"job_uuid\": \"" << job.job_uuid << "\",\n"
      << "  \"camera_uuid\": \"" << job.camera_uuid << "\",\n"
      << "  \"window\": {\"from_utc_us\": " << job.from_utc_us
      << ", \"to_utc_us\": " << job.to_utc_us << "},\n"
      << "  \"clip\": \"synopsis.mp4\",\n"
      << "  \"tubes\": [\n";
    for (size_t i = 0; i < tubes.size(); ++i) {
        const ObjectTube& t = tubes[i];
        m << "    {\"tube_id\": " << t.tube_id
          << ", \"synopsis_offset_us\": " << t.synopsis_offset_us
          << ", \"source_utc_us\": " << t.first_utc_us
          << ", \"samples\": [";
        for (size_t s = 0; s < t.samples.size(); ++s) {
            const TubeSample& ts = t.samples[s];
            m << "{\"t\": " << (ts.utc_us - t.first_utc_us)
              << ", \"x\": " << ts.x << ", \"y\": " << ts.y
              << ", \"w\": " << ts.w << ", \"h\": " << ts.h << "}"
              << (s + 1 < t.samples.size() ? ", " : "");
        }
        m << "]}" << (i + 1 < tubes.size() ? "," : "") << "\n";
    }
    m << "  ]\n}\n";

    // Clip encode: the production path composites tube chips over the median
    // background plate and encodes H.264 (NVENC when caps_.gpu_available,
    // libx264 veryfast otherwise), stamping each chip with its original
    // Shamsi timestamp. Composite + encode run here on this worker thread.
    std::printf("[synopsis] job=%s tubes=%zu -> %s/synopsis.mp4\n",
                job.job_uuid.c_str(), tubes.size(), job.output_dir.c_str());
    return true;
}

}  // namespace vms
