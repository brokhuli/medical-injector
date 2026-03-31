# ── Backend static library (all sources except main.cpp) ─────
# Shared by the executable and test targets to avoid recompilation.
add_library(injector-backend-lib STATIC
    src/placeholder.cpp
)

target_include_directories(injector-backend-lib PUBLIC src)
target_link_libraries(injector-backend-lib PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Warnings
target_compile_options(injector-backend-lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Werror -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Sanitizers in Debug (GCC/Clang only — MSVC uses /fsanitize=address separately)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    target_compile_options(injector-backend-lib PRIVATE
        -fsanitize=address,undefined
    )
    target_link_options(injector-backend-lib PRIVATE
        -fsanitize=address,undefined
    )
endif()

# ── Backend executable ───────────────────────────────────────
add_executable(injector-backend src/main.cpp)
target_link_libraries(injector-backend PRIVATE injector-backend-lib)

# ── Tests ────────────────────────────────────────────────────
add_subdirectory(tests)
