#pragma once
// =============================================================================
// AiInferenceClient — C++ side of the zero-copy AI signaling channel.
//
// Owns the bidirectional AiInference.ProcessFrameStream to the Python AI
// container (default localhost:50061; frames never leave the machine — only
// FrameDescriptors traverse gRPC while pixels live in POSIX shm written by
// ShmFrameWriter as /vms_frame_<cam>_<slot>).
//
// Threading model:
//   - announce_frame() is called from decoder threads (lock-protected write).
//   - A dedicated reader thread drains InferenceResults, releases the shm
//     slot back to the owning ShmFrameWriter ring, and forwards detections
//     to the result callback (which relays them to Django's
//     POST /api/forensic/ingest).
// =============================================================================
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "ai_signaling.grpc.pb.h"

namespace vms {

class ShmFrameWriter;

class AiInferenceClient {
public:
    using ResultCallback = std::function<void(const vms::ai::InferenceResult&)>;

    explicit AiInferenceClient(const std::string& target = "localhost:50061");
    ~AiInferenceClient();

    AiInferenceClient(const AiInferenceClient&) = delete;
    AiInferenceClient& operator=(const AiInferenceClient&) = delete;

    // Register the shm ring for a camera so returned slot_index values can be
    // released back to the right writer. Call before announcing frames.
    void register_writer(const std::string& camera_uuid, ShmFrameWriter* writer);
    void unregister_writer(const std::string& camera_uuid);

    // Detections sink (relay to Django forensic ingest). Set once at startup.
    void set_result_callback(ResultCallback cb);

    // Opens the bidi stream and starts the reader thread. Returns false if
    // the channel cannot connect (caller may retry with backoff).
    bool start();

    // Half-closes the stream, joins the reader thread.
    void stop();

    // Announce a filled shm slot to the AI engine. Non-blocking except for a
    // short stream-write lock. Returns false if the stream is down (caller
    // should release the slot itself and drop the frame).
    bool announce_frame(const std::string& camera_uuid,
                        const std::string& shm_name,
                        uint64_t shm_size_bytes,
                        uint32_t width, uint32_t height, uint32_t stride_bytes,
                        vms::ai::PixelFormat format,
                        uint64_t utc_epoch_ms, uint64_t frame_sequence,
                        uint32_t slot_index);

    // Unary capability negotiation (GPU/TensorRT vs OpenVINO/ONNX INT8).
    bool get_capabilities(vms::ai::AiCapabilities* out);

    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    void reader_loop();

    std::string target_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<vms::ai::AiInference::Stub> stub_;

    std::unique_ptr<grpc::ClientContext> stream_ctx_;
    std::unique_ptr<grpc::ClientReaderWriter<vms::ai::FrameDescriptor,
                                             vms::ai::InferenceResult>> stream_;
    std::mutex write_mutex_;  // gRPC allows one concurrent Write() per stream

    std::thread reader_thread_;
    std::atomic<bool> running_{false};

    std::mutex writers_mutex_;
    std::unordered_map<std::string, ShmFrameWriter*> writers_;

    ResultCallback result_callback_;
};

}  // namespace vms
