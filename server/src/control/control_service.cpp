// =============================================================================
// ControlService — the C++ node's half of the Control Plane channel.
//
//  Registration : ControlPlaneEvents.RegisterServer (blocking retry loop)
//  Event uplink : ControlPlaneEvents.ReportEvent (client stream, reconnects)
//  Command sink : serves vms.control.RecordingServerControl on :50051
//  Signal sink  : minimal RESP2 SUBSCRIBE client on "vms:control-signals"
//                 (no hiredis dependency — raw non-blocking TCP socket)
// =============================================================================
#include "control/control_service.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

#include "control_signals.grpc.pb.h"

namespace vms {

namespace pb = vms::control;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string env_or(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : def;
}

static std::string generate_uuid_v4() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(rng), b = dist(rng);
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<uint32_t>(a >> 32),
                  static_cast<uint32_t>(a >> 16) & 0xffff,
                  static_cast<uint32_t>(a) & 0x0fff,
                  (static_cast<uint32_t>(b >> 48) & 0x3fff) | 0x8000,
                  static_cast<unsigned long long>(b & 0xffffffffffffULL));
    return buf;
}

static std::string local_hostname() {
    char name[256] = "vms-node";
#ifndef _WIN32
    gethostname(name, sizeof(name));
#endif
    return name;
}

static StreamProfile to_stream_profile(pb::StreamProfile p) {
    switch (p) {
        case pb::STREAM_PROFILE_MAIN: return StreamProfile::Main;
        case pb::STREAM_PROFILE_MID:  return StreamProfile::Mid;
        default:                      return StreamProfile::Sub;
    }
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct ControlService::Impl final : public pb::RecordingServerControl::Service {
    explicit Impl(const HardwareCaps& caps) : caps(caps) {
        server_uuid = env_or("VMS_SERVER_UUID", generate_uuid_v4());
        control_plane_target = env_or("VMS_CONTROL_PLANE_GRPC", "localhost:50060");
        listen_port = env_or("VMS_CONTROL_LISTEN_PORT", "50051");
        redis_host = env_or("VMS_REDIS_HOST", "localhost");
        redis_port = static_cast<uint16_t>(
            std::stoi(env_or("VMS_REDIS_PORT", "6379")));

        grpc::ChannelArguments args;
        args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 30'000);
        args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
        channel = grpc::CreateCustomChannel(
            control_plane_target, grpc::InsecureChannelCredentials(), args);
        events_stub = pb::ControlPlaneEvents::NewStub(channel);
    }

    // ---- RecordingServerControl service (called BY Django) -----------------
    grpc::Status ApplyCameraConfig(grpc::ServerContext*,
                                   const pb::CameraConfigRequest* req,
                                   pb::ControlAck* ack) override {
        std::printf("[control] camera config %s action=%d rev=%s\n",
                    req->camera_uuid().c_str(), req->action(),
                    req->config_revision().c_str());
        if (camera_config_cb) {
            uint64_t rev = 0;
            try { rev = std::stoull(req->config_revision()); } catch (...) {}
            camera_config_cb(req->camera_uuid(), rev);
        }
        ack->set_ok(true);
        ack->set_message_fa("پیکربندی دوربین اعمال شد.");
        ack->set_applied_revision(req->config_revision());
        return grpc::Status::OK;
    }

    grpc::Status ResetStream(grpc::ServerContext*,
                             const pb::StreamResetRequest* req,
                             pb::ControlAck* ack) override {
        std::printf("[control] stream reset %s profile=%d (%s)\n",
                    req->camera_uuid().c_str(), req->profile(),
                    req->reason().c_str());
        if (stream_reset_cb)
            stream_reset_cb(req->camera_uuid(), req->profile());
        ack->set_ok(true);
        ack->set_message_fa("جریان دوربین بازنشانی شد.");
        return grpc::Status::OK;
    }

    grpc::Status UpdateMotionRoi(grpc::ServerContext*,
                                 const pb::MotionRoiRequest* req,
                                 pb::ControlAck* ack) override {
        if (motion_roi_cb) {
            std::vector<uint8_t> mask(req->roi_bitmask().begin(),
                                      req->roi_bitmask().end());
            motion_roi_cb(req->camera_uuid(), mask, req->grid_cols(),
                          req->grid_rows(), req->sensitivity());
        }
        ack->set_ok(true);
        ack->set_message_fa("ناحیه حساس حرکت به‌روزرسانی شد.");
        return grpc::Status::OK;
    }

    grpc::Status GetServerHealth(grpc::ServerContext*, const pb::HealthRequest*,
                                 pb::ServerHealthReport* report) override {
        report->set_server_uuid(server_uuid);
        report->set_active_cameras(active_cameras.load());
        report->set_relay_clients(relay_clients.load());
        report->set_gpu_available(caps.gpu_available);
#ifndef _WIN32
        struct sysinfo si {};
        if (sysinfo(&si) == 0) {
            const double used_mb =
                (si.totalram - si.freeram) * si.mem_unit / (1024.0 * 1024.0);
            report->set_ram_used_mb(used_mb);
            // 1-minute loadavg normalized to core count ≈ CPU utilization.
            const long cores = sysconf(_SC_NPROCESSORS_ONLN);
            report->set_cpu_percent(
                100.0 * (si.loads[0] / 65536.0) / std::max(1L, cores));
        }
        struct statvfs vfs {};
        if (statvfs("/mnt/vms-archive", &vfs) == 0) {
            report->set_storage_free_gb(
                vfs.f_bavail * static_cast<double>(vfs.f_frsize) /
                (1024.0 * 1024.0 * 1024.0));
        }
#endif
        report->set_ntp_offset_us(ntp_offset_us.load());
        return grpc::Status::OK;
    }

    grpc::Status PruneArchive(grpc::ServerContext*, const pb::PruneRequest* req,
                              pb::ControlAck* ack) override {
        // Four-Eyes: both admin signatures MUST be present; they were verified
        // cryptographically by Django and are logged into the immutable trail.
        if (req->approval_signature_a().empty() ||
            req->approval_signature_b().empty()) {
            ack->set_ok(false);
            ack->set_message_fa("عملیات حذف نیازمند دو امضای مدیر مجزاست.");
            return grpc::Status::OK;
        }
        bool ok = prune_cb
            ? prune_cb(req->camera_uuid(), req->before_utc_epoch_ms(),
                       req->approval_signature_a(), req->approval_signature_b())
            : false;
        ack->set_ok(ok);
        ack->set_message_fa(ok ? "حذف آرشیو با موفقیت انجام شد."
                               : "حذف آرشیو انجام نشد.");
        return grpc::Status::OK;
    }

    // ---- Event uplink (ReportEvent client stream with reconnect) -----------
    void enqueue_event(pb::ServerEvent&& event) {
        {
            std::lock_guard<std::mutex> lock(event_mutex);
            if (event_queue.size() >= 10'000) event_queue.pop_front();  // bound
            event_queue.push_back(std::move(event));
        }
        event_cv.notify_one();
    }

    void event_uplink_loop(std::atomic<bool>& running) {
        while (running.load()) {
            grpc::ClientContext ctx;
            pb::ControlAck ack;
            auto stream = events_stub->ReportEvent(&ctx, &ack);
            if (!stream) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            bool broken = false;
            while (running.load() && !broken) {
                pb::ServerEvent event;
                {
                    std::unique_lock<std::mutex> lock(event_mutex);
                    event_cv.wait_for(lock, std::chrono::seconds(1), [&] {
                        return !event_queue.empty() || !running.load();
                    });
                    if (event_queue.empty()) continue;
                    event = std::move(event_queue.front());
                    event_queue.pop_front();
                }
                if (!stream->Write(event)) {
                    // Requeue at the front and reconnect with backoff.
                    std::lock_guard<std::mutex> lock(event_mutex);
                    event_queue.push_front(std::move(event));
                    broken = true;
                }
            }
            stream->WritesDone();
            stream->Finish();
            if (broken) std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    // ---- Minimal RESP2 Redis SUBSCRIBE client -------------------------------
    // Django's orchestrator publishes JSON control signals on
    // settings.CONTROL_SIGNAL_CHANNEL; gRPC above is the guaranteed path, the
    // Redis channel is the low-latency fan-out. Payload handling mirrors the
    // gRPC handlers (camera_config / motion_roi / synopsis_* / stream_reset).
    void redis_subscribe_loop(std::atomic<bool>& running) {
#ifndef _WIN32
        while (running.load()) {
            int fd = redis_connect();
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            const std::string cmd =
                "*2\r\n$9\r\nSUBSCRIBE\r\n$19\r\nvms:control-signals\r\n";
            if (send(fd, cmd.data(), cmd.size(), 0) < 0) {
                close(fd);
                continue;
            }
            std::string buffer;
            char chunk[4096];
            while (running.load()) {
                struct timeval tv {1, 0};
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
                if (n == 0) break;                       // server closed
                if (n < 0) continue;                     // timeout -> re-check
                buffer.append(chunk, static_cast<size_t>(n));
                drain_pubsub_messages(buffer);
            }
            close(fd);
        }
#else
        (void)running;  // Windows deployments rely on the gRPC path only.
#endif
    }

#ifndef _WIN32
    int redis_connect() const {
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        if (getaddrinfo(redis_host.c_str(),
                        std::to_string(redis_port).c_str(), &hints, &res) != 0)
            return -1;
        int fd = -1;
        for (auto* p = res; p; p = p->ai_next) {
            fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        return fd;
    }
#endif

    // Extract complete `message` payloads out of the RESP buffer. We only
    // need the bulk-string JSON body (third element of the push array).
    void drain_pubsub_messages(std::string& buffer) {
        size_t pos;
        // Cheap framing: every publish arrives as
        //   *3\r\n$7\r\nmessage\r\n$<ch>\r\n<channel>\r\n$<len>\r\n<json>\r\n
        while ((pos = buffer.find("$7\r\nmessage\r\n")) != std::string::npos) {
            size_t p = buffer.find("\r\n$", pos + 13);          // channel len
            if (p == std::string::npos) return;
            p = buffer.find("\r\n", p + 3);                     // channel body
            if (p == std::string::npos) return;
            size_t len_start = buffer.find("$", p);
            if (len_start == std::string::npos) return;
            size_t len_end = buffer.find("\r\n", len_start);
            if (len_end == std::string::npos) return;
            size_t body_len = 0;
            try {
                body_len = std::stoul(
                    buffer.substr(len_start + 1, len_end - len_start - 1));
            } catch (...) { buffer.erase(0, len_end); continue; }
            if (buffer.size() < len_end + 2 + body_len) return; // incomplete
            std::string json = buffer.substr(len_end + 2, body_len);
            buffer.erase(0, len_end + 2 + body_len + 2);
            handle_control_signal(json);
        }
    }

    void handle_control_signal(const std::string& json) {
        std::printf("[control] redis signal: %s\n", json.c_str());
        // Field extraction without a JSON dependency (payloads are flat).
        auto field = [&](const char* key) -> std::string {
            std::string needle = std::string("\"") + key + "\"";
            size_t k = json.find(needle);
            if (k == std::string::npos) return {};
            size_t colon = json.find(':', k + needle.size());
            if (colon == std::string::npos) return {};
            size_t start = json.find_first_not_of(" \t", colon + 1);
            if (start == std::string::npos) return {};
            if (json[start] == '"') {
                size_t end = json.find('"', start + 1);
                return json.substr(start + 1, end - start - 1);
            }
            size_t end = json.find_first_of(",}", start);
            return json.substr(start, end - start);
        };

        const std::string type = field("type");
        const std::string target = field("server_uuid");
        if (!target.empty() && target != server_uuid) return;  // not for us

        if (type == "camera_config" && camera_config_cb) {
            uint64_t rev = 0;
            try { rev = std::stoull(field("config_revision")); } catch (...) {}
            camera_config_cb(field("camera_uuid"), rev);
        } else if (type == "stream_reset" && stream_reset_cb) {
            stream_reset_cb(field("camera_uuid"), 0);
        }
        // motion_roi / synopsis_request / synopsis_cancel carry binary or
        // large payloads — those flow through the authoritative gRPC path.
    }

    // ---- Data ----------------------------------------------------------------
    HardwareCaps caps;
    std::string server_uuid;
    std::string control_plane_target;
    std::string listen_port;
    std::string redis_host;
    uint16_t redis_port = 6379;

    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<pb::ControlPlaneEvents::Stub> events_stub;

    std::mutex event_mutex;
    std::condition_variable event_cv;
    std::deque<pb::ServerEvent> event_queue;

    std::atomic<uint32_t> active_cameras{0};
    std::atomic<uint32_t> relay_clients{0};
    std::atomic<uint64_t> ntp_offset_us{0};

    CameraConfigHandler camera_config_cb;
    StreamResetHandler stream_reset_cb;
    MotionRoiHandler motion_roi_cb;
    PruneHandler prune_cb;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
ControlService::ControlService(const HardwareCaps& caps)
    : caps_(caps), impl_(std::make_unique<Impl>(caps)) {}

ControlService::~ControlService() = default;

const std::string& ControlService::server_uuid() const {
    return impl_->server_uuid;
}

void ControlService::on_camera_config(CameraConfigHandler cb) {
    impl_->camera_config_cb = std::move(cb);
}
void ControlService::on_stream_reset(StreamResetHandler cb) {
    impl_->stream_reset_cb = std::move(cb);
}
void ControlService::on_motion_roi(MotionRoiHandler cb) {
    impl_->motion_roi_cb = std::move(cb);
}
void ControlService::on_prune(PruneHandler cb) {
    impl_->prune_cb = std::move(cb);
}

ControlService::AssignedCameras ControlService::register_with_control_plane() {
    pb::ServerRegistration reg;
    reg.set_server_uuid(impl_->server_uuid);
    reg.set_hostname(local_hostname());
    reg.set_version("1.0.0");
    reg.set_gpu_available(caps_.gpu_available);
    if (caps_.has_avx2) reg.add_simd_capabilities("AVX2");
    if (caps_.has_avx512) reg.add_simd_capabilities("AVX512");

    // Blocking retry with capped exponential backoff — the recording server
    // is useless without its assignment, so we insist until Django answers.
    int backoff_s = 1;
    for (;;) {
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::seconds(5));
        pb::AssignedCameraSet assigned;
        grpc::Status status =
            impl_->events_stub->RegisterServer(&ctx, reg, &assigned);
        if (status.ok()) {
            AssignedCameras out;
            for (const auto& cam : assigned.cameras()) {
                out.camera_uuids.push_back(cam.camera_uuid());
                std::vector<CameraStreamConfig> configs;
                for (const auto& ep : cam.streams()) {
                    configs.push_back(CameraStreamConfig{
                        cam.camera_uuid(), ep.url(),
                        to_stream_profile(ep.profile()), ep.codec()});
                }
                out.stream_configs.push_back(std::move(configs));
            }
            impl_->active_cameras.store(
                static_cast<uint32_t>(out.camera_uuids.size()));
            std::printf("[control] registered uuid=%s cameras=%zu\n",
                        impl_->server_uuid.c_str(), out.camera_uuids.size());
            return out;
        }
        std::printf("[control] registration failed (%s), retrying in %ds\n",
                    status.error_message().c_str(), backoff_s);
        std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
        backoff_s = std::min(backoff_s * 2, 30);
    }
}

void ControlService::report_motion(const std::string& camera_uuid, bool started,
                                   uint64_t utc_us) {
    report_event(camera_uuid,
                 started ? pb::EVENT_MOTION_STARTED : pb::EVENT_MOTION_STOPPED,
                 utc_us / 1000, "{}");
}

void ControlService::report_event(const std::string& camera_uuid, int type,
                                  uint64_t utc_ms,
                                  const std::string& payload_json) {
    pb::ServerEvent event;
    event.set_server_uuid(impl_->server_uuid);
    event.set_camera_uuid(camera_uuid);
    event.set_type(static_cast<pb::EventType>(type));
    event.set_utc_epoch_ms(utc_ms);
    event.set_payload_json(payload_json);
    impl_->enqueue_event(std::move(event));
}

void ControlService::serve_blocking(std::atomic<bool>& running) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:" + impl_->listen_port,
                             grpc::InsecureServerCredentials());
    builder.RegisterService(impl_.get());
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    std::printf("[control] RecordingServerControl serving on :%s\n",
                impl_->listen_port.c_str());

    std::thread uplink([&] { impl_->event_uplink_loop(running); });
    std::thread redis([&] { impl_->redis_subscribe_loop(running); });

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    server->Shutdown(std::chrono::system_clock::now() +
                     std::chrono::seconds(3));
    impl_->event_cv.notify_all();
    uplink.join();
    redis.join();
}

}  // namespace vms
