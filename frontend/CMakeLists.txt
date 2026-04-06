cmake_minimum_required(VERSION 3.22)

# ── Qt6 ─────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup()

# ── Sources ─────────────────────────────────────────────────────
set(FRONTEND_SOURCES
    src/main.cpp
    src/grpc/GrpcClientService.h
    src/grpc/GrpcClientService.cpp
    src/bridge/InjectorBridge.h
    src/bridge/InjectorBridge.cpp
)

qt_add_executable(injector-frontend ${FRONTEND_SOURCES})

# ── Qt Resources (QML files) ───────────────────────────────────
qt_add_resources(injector-frontend "qml"
    PREFIX "/"
    FILES
        qml/main.qml
        qml/Theme.qml
        qml/qmldir
        qml/components/ConnectionIndicator.qml
        qml/components/StateIndicator.qml
        qml/components/PhaseRow.qml
        qml/components/SyringeIndicator.qml
        qml/components/ProtocolPanel.qml
        qml/components/ControlPanel.qml
        qml/components/FaultDetail.qml
        qml/components/FlowRateGauge.qml
        qml/components/PressureGauge.qml
        qml/components/VolumeProgress.qml
        qml/components/ElapsedTimer.qml
        qml/components/TimelineChart.qml
        qml/components/Dashboard.qml
        qml/components/EventLog.qml
)

# ── Include paths ───────────────────────────────────────────────
target_include_directories(injector-frontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Link ────────────────────────────────────────────────────────
target_link_libraries(injector-frontend PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    injector-proto
)

# ── Windows: deploy Qt DLLs next to executable ─────────────────
if(WIN32)
    # Copy Qt runtime DLLs for development convenience
    qt_generate_deploy_app_script(
        TARGET injector-frontend
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()
