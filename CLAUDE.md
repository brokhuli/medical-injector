# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Medical contrast power injector simulator. Two-process architecture: C++ backend (real-time control engine + gRPC server) and Qt/QML frontend (clinical UI + gRPC client). Educational/research tool, not a medical device.

## Build & Development Commands

- Configure: `cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
- Build all: `cmake --build build --config Release`
- Build backend only: `cmake --build build --config Release --target injector-backend`
- Run tests: `ctest --test-dir build -C Release --output-on-failure`
- Run backend: `./build/backend/Release/injector-backend.exe`

Note: On Windows, commands must be run from a VS Developer Shell (`Launch-VsDevShell.ps1 -Arch amd64`).

## Architecture

Two-process, 7-layer backend architecture. See `spec/` for full details.

## Conventions

- C++17. Namespace: `injector::*`.
- `PascalCase` for types/files, `camelCase` for functions/variables, `trailing_` underscore for private members.
- Static library pattern: `injector-backend-lib` (all sources except main.cpp) shared by executable and test targets.
- Tests: `backend/tests/test_*.cpp`, run via CTest.
