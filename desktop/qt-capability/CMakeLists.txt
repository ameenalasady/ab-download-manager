cmake_minimum_required(VERSION 3.21)

project(QtCapabilityShell VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Widgets)
find_package(Qt6 6.7 QUIET COMPONENTS Test)

# KF6WindowSystem's CMake config calls find_dependency(X11), which needs an
# X11Config.cmake that this Wayland-only workstation does not provide. Its
# pkg-config file is sufficient for the optional blur bridge.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(KF6WindowSystemPC QUIET IMPORTED_TARGET KF6WindowSystem)
endif()
if(TARGET PkgConfig::KF6WindowSystemPC)
    message(STATUS "qt-capability: KF6WindowSystem blur bridge enabled")
else()
    message(STATUS "qt-capability: KF6WindowSystem blur bridge unavailable")
endif()

add_subdirectory(../qt-ui "${CMAKE_CURRENT_BINARY_DIR}/qt-ui")

# Read-only idle helper; no Qt window and no input injection. Protocol code is generated
# from the system wayland-protocols package (build-time only).
if(PkgConfig_FOUND)
    pkg_check_modules(WaylandClient QUIET IMPORTED_TARGET wayland-client)
    pkg_get_variable(WaylandProtocolsDirectory wayland-protocols pkgdatadir)
endif()
find_program(WAYLAND_SCANNER wayland-scanner)
set(IDLE_PROTOCOL "${WaylandProtocolsDirectory}/staging/ext-idle-notify/ext-idle-notify-v1.xml")
set(IDLE_INPUT_PROTOCOL_AVAILABLE FALSE)
if(EXISTS "${IDLE_PROTOCOL}")
    file(READ "${IDLE_PROTOCOL}" IDLE_PROTOCOL_CONTENT)
    if(IDLE_PROTOCOL_CONTENT MATCHES "get_input_idle_notification")
        set(IDLE_INPUT_PROTOCOL_AVAILABLE TRUE)
    endif()
endif()
if(TARGET PkgConfig::WaylandClient AND WAYLAND_SCANNER AND IDLE_INPUT_PROTOCOL_AVAILABLE)
    add_custom_command(OUTPUT ext-idle-notify-v1-client-protocol.h ext-idle-notify-v1-protocol.c
        COMMAND "${WAYLAND_SCANNER}" client-header "${IDLE_PROTOCOL}" ext-idle-notify-v1-client-protocol.h
        COMMAND "${WAYLAND_SCANNER}" private-code "${IDLE_PROTOCOL}" ext-idle-notify-v1-protocol.c
        DEPENDS "${IDLE_PROTOCOL}" VERBATIM)
    add_executable(abdm-activity-monitor src/activitymonitor.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h"
        "${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c")
    target_include_directories(abdm-activity-monitor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(abdm-activity-monitor PRIVATE PkgConfig::WaylandClient)
endif()

find_package(KF6Kirigami QUIET)
if(KF6Kirigami_FOUND)
    message(STATUS "qt-capability: optional Kirigami example available at runtime")
else()
    message(STATUS "qt-capability: optional Kirigami example not found")
endif()

add_library(qtcapability-geometry STATIC
    src/regiongeometry.cpp
    src/regiongeometry.h
)
target_include_directories(qtcapability-geometry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(qtcapability-geometry PUBLIC Qt6::Gui)

add_executable(qt-capability-shell
    src/main.cpp
    src/desktopadapters.cpp
    src/desktopadapters.h
    src/capabilitywindow.cpp
    src/capabilitywindow.h
)
target_include_directories(qt-capability-shell PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(qt-capability-shell PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    qtui-preview
)

qt_add_resources(qt-capability-shell "shell-qml"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/Backdrop.qml
        qml/KirigamiDialogProbe.qml
)
# The original application icon (desktop/app/icons/icon.png, same bytes as the installed
# ABDownloadManager.png): window icon on every toplevel via xdg-toplevel-icon.
qt_add_resources(qt-capability-shell "shell-icon"
    PREFIX "/icons"
    BASE "../app/icons"
    FILES ../app/icons/icon.png
)

if(TARGET PkgConfig::KF6WindowSystemPC)
    target_link_libraries(qt-capability-shell PRIVATE PkgConfig::KF6WindowSystemPC)
    target_compile_definitions(qt-capability-shell PRIVATE QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM=1)
endif()

if(BUILD_TESTING AND TARGET Qt6::Test)
    add_executable(tst_regiongeometry tests/tst_regiongeometry.cpp)
    target_link_libraries(tst_regiongeometry PRIVATE qtcapability-geometry Qt6::Test Qt6::Gui)
    add_test(NAME qt-capability-regiongeometry COMMAND tst_regiongeometry)
    set_tests_properties(qt-capability-regiongeometry PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(tst_qmlload tests/tst_qmlload.cpp)
    target_link_libraries(tst_qmlload PRIVATE
        qtui-preview
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::Test
    )
    qt_add_resources(tst_qmlload "test-qml"
        PREFIX "/"
        FILES
            qml/Main.qml
            qml/Backdrop.qml
            qml/KirigamiDialogProbe.qml
    )
    add_test(NAME qt-capability-qmlload COMMAND tst_qmlload)
    set_tests_properties(qt-capability-qmlload PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )
endif()
