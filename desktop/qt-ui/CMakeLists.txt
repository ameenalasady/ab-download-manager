# First migration slice. Hosted by qt-capability-shell until service IPC exists.
find_package(Qt6 6.7 REQUIRED COMPONENTS Network DBus)
add_library(qtui-preview STATIC src/previewdownloads.cpp src/previewdownloads.h
    src/servicedownloads.cpp src/servicedownloads.h)
target_include_directories(qtui-preview PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(qtui-preview PUBLIC Qt6::Core Qt6::DBus Qt6::Network Qt6::Quick Qt6::Widgets)
qt_add_qml_module(qtui-preview URI Abdm.Desktop VERSION 1.0 NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Abdm/Desktop"
    SOURCES src/nativewindowstyle.cpp src/nativewindowstyle.h src/desktopopener.cpp src/desktopopener.h
    src/aboutcredits.cpp src/aboutcredits.h src/downloadpartslots.cpp src/downloadpartslots.h)
target_sources(qtui-preview PRIVATE ../qt-capability/src/kwindowsystembridge.cpp)
target_include_directories(qtui-preview PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../qt-capability/src")
if(TARGET PkgConfig::KF6WindowSystemPC)
    target_link_libraries(qtui-preview PUBLIC PkgConfig::KF6WindowSystemPC)
    target_compile_definitions(qtui-preview PRIVATE QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM=1)
endif()
qt_add_resources(qtui-preview preview_qml
    PREFIX "/qt-ui"
    FILES qml/qmldir qml/Tokens.qml qml/SectionHeading.qml qml/TextBlock.qml qml/StatusMessage.qml qml/DuplicateNotice.qml qml/ChecksumWorkflow.qml
    qml/FormGrid.qml qml/FormLabel.qml qml/FormField.qml qml/SurfaceFrame.qml qml/SettingsSection.qml
    qml/ProgressTrack.qml qml/WindowFooter.qml qml/ButtonFocus.qml qml/UiButton.qml qml/UiComboBox.qml qml/UiTabButton.qml
    qml/UiDialog.qml qml/DialogFooter.qml qml/IconButton.qml qml/SidebarItem.qml qml/HttpOptions.qml qml/DownloadsView.qml qml/DownloadDetails.qml qml/DownloadContent.qml qml/DownloadCompletion.qml qml/DownloadWindow.qml qml/ServiceWorkflows.qml qml/CategoryWorkflows.qml qml/BrowserRequests.qml qml/PreviewTheme.qml qml/LegacyIcon.qml qml/LegacyPaths.js qml/CategoryIcons.js
)
# About → Licenses / Translators: the original project's license and translator credits.
set(ABDM_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set_source_files_properties("${ABDM_REPOSITORY_ROOT}/LICENSE" PROPERTIES QT_RESOURCE_ALIAS LICENSE)
set_source_files_properties("${ABDM_REPOSITORY_ROOT}/shared/resources/src/commonMain/resources/com/abdownloadmanager/resources/credits/translators.json"
    PROPERTIES QT_RESOURCE_ALIAS translators.json)
qt_add_resources(qtui-preview about_credits
    PREFIX "/qt-ui/credits"
    FILES "${ABDM_REPOSITORY_ROOT}/LICENSE"
    "${ABDM_REPOSITORY_ROOT}/shared/resources/src/commonMain/resources/com/abdownloadmanager/resources/credits/translators.json"
)

if(BUILD_TESTING AND TARGET Qt6::Test)
    add_executable(tst_previewdownloads tests/tst_previewdownloads.cpp)
    target_link_libraries(tst_previewdownloads PRIVATE qtui-preview Qt6::Test)
    add_test(NAME qt-ui-previewdownloads COMMAND tst_previewdownloads)

    add_executable(tst_servicedownloads tests/tst_servicedownloads.cpp)
    target_link_libraries(tst_servicedownloads PRIVATE qtui-preview Qt6::Test
        Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
    add_test(NAME qt-ui-servicedownloads COMMAND tst_servicedownloads)
    set_tests_properties(qt-ui-servicedownloads PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_executable(tst_desktopopener tests/tst_desktopopener.cpp)
        target_link_libraries(tst_desktopopener PRIVATE qtui-preview Qt6::Test Qt6::DBus Qt6::Widgets)
        add_test(NAME qt-ui-desktopopener COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:tst_desktopopener>)
        set_tests_properties(qt-ui-desktopopener PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ABDM_OPENER_PRIVATE_BUS=1")
    endif()

    add_executable(tst_nativewindowstyle tests/tst_nativewindowstyle.cpp)
    target_link_libraries(tst_nativewindowstyle PRIVATE qtui-preview Qt6::Test Qt6::Quick Qt6::Widgets)
    add_test(NAME qt-ui-nativewindowstyle COMMAND tst_nativewindowstyle)
    set_tests_properties(qt-ui-nativewindowstyle PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    add_executable(tst_downloadsview tests/tst_downloadsview.cpp)
    target_link_libraries(tst_downloadsview PRIVATE qtui-preview Qt6::Test
        Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
    add_test(NAME qt-ui-downloadsview COMMAND tst_downloadsview)
    set_tests_properties(qt-ui-downloadsview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
