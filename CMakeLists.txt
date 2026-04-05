cmake_minimum_required(VERSION 3.22)
project(medical-injector VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ── Proto generation ─────────────────────────────────────────
# Separate static library so proto headers don't pollute non-gRPC targets.
add_library(injector-proto STATIC)

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/injector.proto")

# Find the protoc and grpc_cpp_plugin executables
find_program(PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_OUT_DIR}")

set(PROTO_SRCS "${PROTO_OUT_DIR}/injector.pb.cc")
set(PROTO_HDRS "${PROTO_OUT_DIR}/injector.pb.h")
set(GRPC_SRCS  "${PROTO_OUT_DIR}/injector.grpc.pb.cc")
set(GRPC_HDRS  "${PROTO_OUT_DIR}/injector.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
        --proto_path="${CMAKE_CURRENT_SOURCE_DIR}/proto"
        --cpp_out="${PROTO_OUT_DIR}"
        --grpc_out="${PROTO_OUT_DIR}"
        --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN}"
        "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
    COMMENT "Generating protobuf/gRPC sources from injector.proto"
)

target_sources(injector-proto PRIVATE ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(injector-proto PUBLIC "${PROTO_OUT_DIR}")
target_link_libraries(injector-proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)

# Suppress warnings in generated code
target_compile_options(injector-proto PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)

# ── Testing ──────────────────────────────────────────────────
enable_testing()

# ── Subdirectories ───────────────────────────────────────────
add_subdirectory(backend)
