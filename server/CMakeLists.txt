# =============================================================================
# VMS Recording Server & Media Relay Engine — Native C++ Data Plane
# Requires: FFmpeg (libav*), gRPC/protobuf, and a C++20 compiler.
# GPU builds additionally link CUDA / OpenCV-CUDA for Layer-2 motion detection.
# =============================================================================
cmake_minimum_required(VERSION 3.24)
project(vms_recording_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VMS_WITH_CUDA "Build Layer-2 GPU motion detection (MOG2/KNN on CUDA)" OFF)
option(VMS_WITH_AVX512 "Enable AVX-512 paths in the SIMD motion engine" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Threads REQUIRED)

# --- Generate gRPC stubs from ../proto ---------------------------------------
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})
set(PROTO_FILES
    ${PROTO_DIR}/control_signals.proto
    ${PROTO_DIR}/ai_signaling.proto
    ${PROTO_DIR}/media_relay.proto)
# (protobuf_generate + grpc_cpp_plugin wiring)
protobuf_generate(LANGUAGE cpp PROTOS ${PROTO_FILES}
    IMPORT_DIRS ${PROTO_DIR} PROTOC_OUT_DIR ${GEN_DIR} OUT_VAR PROTO_SRCS)
protobuf_generate(LANGUAGE grpc PROTOS ${PROTO_FILES}
    IMPORT_DIRS ${PROTO_DIR} PROTOC_OUT_DIR ${GEN_DIR} OUT_VAR GRPC_SRCS
    PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc)

add_executable(vms_server
    src/main.cpp
    src/ingest/stream_ingestor.cpp
    src/archiver/raw_archiver.cpp
    src/relay/media_relay_engine.cpp
    src/relay/ring_buffer.cpp
    src/motion/motion_engine.cpp
    src/motion/simd_frame_diff.cpp
    src/ipc/shm_frame_writer.cpp
    src/ipc/ai_inference_client.cpp
    src/control/control_service.cpp
    src/ntp/ntp_clock.cpp
    src/synopsis/time_compressor.cpp
    src/edge/edge_retrieval.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(vms_server PRIVATE src ${GEN_DIR})
target_link_libraries(vms_server PRIVATE
    PkgConfig::FFMPEG protobuf::libprotobuf gRPC::grpc++ Threads::Threads)

if (UNIX)
    target_link_libraries(vms_server PRIVATE rt)   # shm_open / POSIX SHM
endif()

# SIMD flags — AVX2 baseline; AVX-512 opt-in (runtime dispatch in motion engine)
include(CheckCXXCompilerFlag)
if (MSVC)
    target_compile_options(vms_server PRIVATE /arch:AVX2)
else()
    target_compile_options(vms_server PRIVATE -mavx2)
    if (VMS_WITH_AVX512)
        target_compile_options(vms_server PRIVATE -mavx512f -mavx512bw)
        target_compile_definitions(vms_server PRIVATE VMS_HAVE_AVX512=1)
    endif()
endif()

if (VMS_WITH_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_sources(vms_server PRIVATE src/motion/gpu_bg_subtraction.cu)
    target_link_libraries(vms_server PRIVATE CUDA::cudart)
    target_compile_definitions(vms_server PRIVATE VMS_HAVE_CUDA=1)
endif()
