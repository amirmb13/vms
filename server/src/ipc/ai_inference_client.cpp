// =============================================================================
// AiInferenceClient implementation.
//
// Zero-copy contract recap (ai_signaling.proto):
//   - ShmFrameWriter creates "/vms_frame_<cam>_<slot>" POSIX shm slots and the
//     decoder renders NV12/BGR frames straight into them.
//   - announce_frame() streams only the FrameDescriptor (name + geometry +
//     NTP timestamp + slot index) over ProcessFrameStream.
//   - The Python engine maps the block via multiprocessing.shared_memory and
//     answers with InferenceResult; slot_index hands ownership back here.
// =============================================================================
#include "ipc/ai_inference_client.h"

#include <chrono>
#include <utility>

#include "ipc/shm_frame_writer.h"

namespace vms {

AiInferenceClient::AiInferenceClient(const std::string& target)
    : target_(target) {
    grpc::ChannelArguments args;
    // Local loopback channel; keepalive keeps the bidi stream healthy across
    // idle periods (e.g. cameras with no motion-gated AI frames for a while).
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 30'000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10'000);
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    channel_ = grpc::CreateCustomChannel(
        target_, grpc::InsecureChannelCredentials(), args);
    stub_ = vms::ai::AiInference::NewStub(channel_);
}

AiInferenceClient::~AiInferenceClient() { stop(); }

void AiInferenceClient::register_writer(const std::string& camera_uuid,
                                        ShmFrameWriter* writer) {
    std::lock_guard<std::mutex> lock(writers_mutex_);
    writers_[camera_uuid] = writer;
}

void AiInferenceClient::unregister_writer(const std::string& camera_uuid) {
    std::lock_guard<std::mutex> lock(writers_mutex_);
    writers_.erase(camera_uuid);
}

void AiInferenceClient::set_result_callback(ResultCallback cb) {
    result_callback_ = std::move(cb);
}

bool AiInferenceClient::start() {
    if (running_.load(std::memory_order_acquire)) return true;

    // Wait briefly for the AI container to accept connections.
    const auto deadline =
        std::chrono::system_clock::now() + std::chrono::seconds(5);
    if (!channel_->WaitForConnected(deadline)) return false;

    stream_ctx_ = std::make_unique<grpc::ClientContext>();
    stream_ = stub_->ProcessFrameStream(stream_ctx_.get());
    if (!stream_) return false;

    running_.store(true, std::memory_order_release);
    reader_thread_ = std::thread([this] { reader_loop(); });
    return true;
}

void AiInferenceClient::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (stream_) stream_->WritesDone();  // half-close; reader drains
    }
    if (reader_thread_.joinable()) reader_thread_.join();

    if (stream_) {
        grpc::Status status = stream_->Finish();
        (void)status;  // logged by caller's telemetry if needed
    }
    stream_.reset();
    stream_ctx_.reset();
}

bool AiInferenceClient::announce_frame(
    const std::string& camera_uuid, const std::string& shm_name,
    uint64_t shm_size_bytes, uint32_t width, uint32_t height,
    uint32_t stride_bytes, vms::ai::PixelFormat format, uint64_t utc_epoch_ms,
    uint64_t frame_sequence, uint32_t slot_index) {
    if (!running_.load(std::memory_order_acquire)) return false;

    vms::ai::FrameDescriptor desc;
    desc.set_shm_name(shm_name);
    desc.set_shm_size_bytes(shm_size_bytes);
    desc.set_width(width);
    desc.set_height(height);
    desc.set_stride_bytes(stride_bytes);
    desc.set_format(format);
    desc.set_camera_uuid(camera_uuid);
    desc.set_utc_epoch_ms(utc_epoch_ms);
    desc.set_frame_sequence(frame_sequence);
    desc.set_slot_index(slot_index);

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (!stream_ || !stream_->Write(desc)) {
        // Stream broke: mark down so the supervisor restarts with backoff.
        running_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

bool AiInferenceClient::get_capabilities(vms::ai::AiCapabilities* out) {
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(3));
    vms::ai::CapabilityRequest request;
    return stub_->GetCapabilities(&ctx, request, out).ok();
}

void AiInferenceClient::reader_loop() {
    vms::ai::InferenceResult result;
    while (stream_->Read(&result)) {
        // 1) Return the shm slot to the owning camera ring FIRST so the
        //    decoder never starves on slots while we post-process results.
        {
            std::lock_guard<std::mutex> lock(writers_mutex_);
            auto it = writers_.find(result.camera_uuid());
            if (it != writers_.end() && it->second != nullptr) {
                it->second->release_slot(result.slot_index());
            }
        }

        // 2) Forward detections (faces + objects + forensic attributes) to
        //    the orchestrator relay -> Django POST /api/forensic/ingest.
        if (result_callback_ &&
            (result.faces_size() > 0 || result.objects_size() > 0)) {
            result_callback_(result);
        }
    }
    // Server closed or stream errored — flip state so announce_frame() fails
    // fast and the supervisor can re-start() the channel.
    running_.store(false, std::memory_order_release);
}

}  // namespace vms
