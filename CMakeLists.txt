cmake_minimum_required(VERSION 3.19)
project(FT891Remote VERSION 0.1.25 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# rcc compresses with zstd by default and then guards on
# qResourceFeatureZstd, which QtCore only exports when it was built with zstd.
# A binary linked against a Qt that has it refuses to start on one that has
# not. zlib is in every Qt build: the guard disappears.
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(FT891_BUILD_SERVER "Build the station server" ON)
option(FT891_BUILD_CLIENT "Build the QML client (desktop and Android)" ON)
option(FT891_BUILD_TESTS  "Build the CAT engine test harness" OFF)

if(ANDROID)
    # No serial port on the phone: the client only.
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2)
    set(FT891_BUILD_SERVER OFF)
    set(FT891_BUILD_TESTS OFF)
    set(FT891_BUILD_CLIENT ON)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
    # The serial port only matters next to the radio: a client-only build
    # does not need the module installed.
    if(FT891_BUILD_SERVER OR FT891_BUILD_TESTS)
        find_package(Qt6 REQUIRED COMPONENTS SerialPort)
    endif()
    if(FT891_BUILD_SERVER)
        find_package(Qt6 REQUIRED COMPONENTS Widgets)
    endif()
    if(FT891_BUILD_CLIENT)
        find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
    endif()
endif()

# ------------------------------------------------------------- resources
# Attached to each executable rather than to the static library: a resource
# buried in an archive is dropped by the linker when nothing references it,
# and the program would start without its CAT description.
set(RR_CAT_QRC   ${CMAKE_CURRENT_SOURCE_DIR}/data/cat/cat.qrc)
set(RR_ICONS_QRC ${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.qrc)

# Translations: none yet. When i18n/ft891remote_fr.ts exists, it is compiled
# and embedded; the code already looks for :/i18n/ft891remote_<lang>.qm.
set(RR_I18N_QRC "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/translations.qrc)
    set(RR_I18N_QRC ${CMAKE_CURRENT_SOURCE_DIR}/i18n/translations.qrc)
endif()

# Executable icon and version metadata, Windows only.
set(RR_SERVER_RC "")
set(RR_CLIENT_RC "")
if(WIN32)
    set(RR_ICON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/icons)
    configure_file(server/appicon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/server_appicon.rc @ONLY)
    configure_file(client/appicon.rc.in ${CMAKE_CURRENT_BINARY_DIR}/client_appicon.rc @ONLY)
    set(RR_SERVER_RC ${CMAKE_CURRENT_BINARY_DIR}/server_appicon.rc)
    set(RR_CLIENT_RC ${CMAKE_CURRENT_BINARY_DIR}/client_appicon.rc)
endif()

# ---------------------------------------------------------- audio backend
# PortAudio has no Android host API upstream, so Android uses Oboe. Both sit
# behind the same AudioEngine interface.
if(ANDROID)
    include(FetchContent)
    # Oboe tags its releases without a prefix: 1.10.0, not v1.10.0.
    FetchContent_Declare(oboe
        GIT_REPOSITORY https://github.com/google/oboe.git
        GIT_TAG        1.10.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(oboe)
    set(RR_AUDIO_SOURCES common/audioengine_oboe.cpp)
    message(STATUS "Audio backend: Oboe")
else()
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
    find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_x64 portaudio_static)
    if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
        message(FATAL_ERROR "PortAudio not found. Debian: apt install portaudio19-dev. "
                            "Windows: vcpkg install portaudio:x64-windows")
    endif()
    set(RR_AUDIO_SOURCES common/audioengine.cpp)
    message(STATUS "Audio backend: PortAudio")
endif()

# ------------------------------------------------------------------- Opus
option(WITH_OPUS "Compress audio with Opus" ON)
set(OPUS_FROM_SOURCE FALSE)
if(WITH_OPUS AND ANDROID)
    # No system Opus on Android: built from source, like Oboe. Without it the
    # APK would fall back to raw PCM, 768 kbit/s over mobile data.
    include(FetchContent)
    FetchContent_Declare(opus
        GIT_REPOSITORY https://github.com/xiph/opus.git
        GIT_TAG        v1.5.2
        GIT_SHALLOW    TRUE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opus)
    set(HAVE_OPUS TRUE)
    set(OPUS_FROM_SOURCE TRUE)
    message(STATUS "Opus: built from source (v1.5.2)")
else()
    find_path(OPUS_INCLUDE_DIR opus/opus.h)
    find_library(OPUS_LIBRARY NAMES opus)
    if(WITH_OPUS AND OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
        set(HAVE_OPUS TRUE)
        message(STATUS "Opus: system library")
    else()
        set(HAVE_OPUS FALSE)
        message(WARNING "Opus not found: only 16-bit PCM will be available")
    endif()
endif()

# ----------------------------------------------------------------- common
add_library(rr_common STATIC
    common/protocol.cpp
    common/chacha20.cpp
    common/crypto.cpp
    common/audiocodec.cpp
    common/resampler.cpp
    common/catcommands.cpp
    common/ft891.cpp
    common/speechproc.cpp
    common/audioengine_shared.cpp
    ${RR_AUDIO_SOURCES}
)
target_include_directories(rr_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rr_common PUBLIC RR_VERSION="${PROJECT_VERSION}")
target_link_libraries(rr_common PUBLIC Qt6::Core Qt6::Network)
if(ANDROID)
    if(TARGET oboe::oboe)
        target_link_libraries(rr_common PUBLIC oboe::oboe)
    else()
        target_link_libraries(rr_common PUBLIC oboe)
    endif()
    target_include_directories(rr_common PUBLIC ${oboe_SOURCE_DIR}/include)
else()
    target_include_directories(rr_common PUBLIC ${PORTAUDIO_INCLUDE_DIR})
    target_link_libraries(rr_common PUBLIC ${PORTAUDIO_LIBRARY})
endif()
if(HAVE_OPUS)
    if(OPUS_FROM_SOURCE)
        target_link_libraries(rr_common PUBLIC Opus::opus)
        # The source tree lays the headers out flat, unlike a distribution
        # package, so audiocodec.cpp includes <opus.h> instead.
        target_compile_definitions(rr_common PUBLIC RR_OPUS_FLAT_HEADERS)
    else()
        target_include_directories(rr_common PUBLIC ${OPUS_INCLUDE_DIR})
        target_link_libraries(rr_common PUBLIC ${OPUS_LIBRARY})
    endif()
    target_compile_definitions(rr_common PUBLIC RR_HAVE_OPUS)
endif()

# The CAT engine: serial link and controller. Shared by the server and the
# test harness.
if(NOT ANDROID AND (FT891_BUILD_SERVER OR FT891_BUILD_TESTS))
    add_library(rr_cat STATIC
        server/ft891link.cpp
        server/ft891controller.cpp
        server/serialports.cpp
    )
    target_link_libraries(rr_cat PUBLIC rr_common Qt6::SerialPort)
endif()

# ----------------------------------------------------------------- server
if(FT891_BUILD_SERVER AND NOT ANDROID)
    add_executable(ft891remote-server WIN32
        server/main.cpp
        server/serverwindow.cpp
        server/servercore.cpp
        server/serverdaemon.cpp
        server/station.cpp
        server/cwkeyer.cpp
        common/i18n.cpp
        ${RR_CAT_QRC}
        ${RR_ICONS_QRC}
        ${RR_I18N_QRC}
        ${RR_SERVER_RC}
    )
    target_link_libraries(ft891remote-server PRIVATE rr_cat Qt6::Widgets)
endif()

# ----------------------------------------------------------------- client
# One Qt Quick client for the desktop and for Android.
if(FT891_BUILD_CLIENT)
    set(RR_CLIENT_SOURCES
        client/qml/main.cpp
        client/qml/ft891bridge.cpp
        client/qml/androidservice.cpp
        client/clientcore.cpp
        client/rigctldserver.cpp
        common/i18n.cpp
        ${RR_CAT_QRC}
        ${RR_ICONS_QRC}
        ${RR_I18N_QRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/client/qml/qml.qrc)

    if(ANDROID)
        qt_add_executable(ft891remote ${RR_CLIENT_SOURCES})
        # The manifest is generated from a template in a copy of the Android
        # folder under the build directory: nothing is written into the
        # sources, and the version follows project().
        math(EXPR RR_ANDROID_VERSION_CODE
             "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
        set(RR_ANDROID_PACKAGE_DIR ${CMAKE_BINARY_DIR}/android-package)
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/android/
             DESTINATION ${RR_ANDROID_PACKAGE_DIR}
             PATTERN "AndroidManifest.xml.in" EXCLUDE)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml.in
                       ${RR_ANDROID_PACKAGE_DIR}/AndroidManifest.xml @ONLY)
        set_property(TARGET ft891remote PROPERTY
            QT_ANDROID_PACKAGE_SOURCE_DIR ${RR_ANDROID_PACKAGE_DIR})
        set(RR_ANDROID_MIN_SDK 26 CACHE STRING "Oldest Android release supported")
        set(RR_ANDROID_TARGET_SDK 36 CACHE STRING "Android API the app is compiled against")
        set_property(TARGET ft891remote PROPERTY QT_ANDROID_MIN_SDK_VERSION ${RR_ANDROID_MIN_SDK})
        set_property(TARGET ft891remote PROPERTY QT_ANDROID_TARGET_SDK_VERSION ${RR_ANDROID_TARGET_SDK})
        # AGP 9 ignores the package attribute of the manifest.
        set_property(TARGET ft891remote PROPERTY QT_ANDROID_PACKAGE_NAME "org.ft891remote.client")
        set_property(TARGET ft891remote PROPERTY QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
        set_property(TARGET ft891remote PROPERTY QT_ANDROID_VERSION_CODE ${RR_ANDROID_VERSION_CODE})
        message(STATUS "Android version ${PROJECT_VERSION} (code ${RR_ANDROID_VERSION_CODE})")
        # androiddeployqt scans the QML imports to decide which plugins to
        # ship. Without this path it may find none, and the app starts on
        # "module QtQuick.Controls plugin not found".
        set_property(TARGET ft891remote PROPERTY
            QT_QML_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/client/qml)
    else()
        add_executable(ft891remote WIN32 ${RR_CLIENT_SOURCES} ${RR_CLIENT_RC})
    endif()
    target_include_directories(ft891remote PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/client)
    target_link_libraries(ft891remote PRIVATE rr_common Qt6::Quick Qt6::QuickControls2)
endif()

# ------------------------------------------------------------------ tests
# A command-line harness that drives the CAT engine against a serial port —
# a real FT-891, or the simulator in test/ft891_sim.py on a pseudo-terminal.
if(FT891_BUILD_TESTS AND NOT ANDROID)
    add_executable(ft891-cattest test/cattest.cpp ${RR_CAT_QRC})
    target_link_libraries(ft891-cattest PRIVATE rr_cat)
    # Recognition of the FT-891's serial ports, without the hardware.
    add_executable(ft891-portstest test/serialports_test.cpp)
    target_link_libraries(ft891-portstest PRIVATE rr_cat)
    # The client's rigctld port, as fldigi and WSJT-X speak to it.
    add_executable(ft891-rigctldtest test/rigctld_test.cpp client/rigctldserver.cpp)
    target_link_libraries(ft891-rigctldtest PRIVATE rr_common)

    # The frequency parser, forms typed and forms shown.
    add_executable(ft891-freqtest test/frequency_test.cpp)
    target_link_libraries(ft891-freqtest PRIVATE rr_common)
    # The guided test on a real radio, with snapshot and restore.
    add_executable(ft891-fieldtest test/fieldtest.cpp ${RR_CAT_QRC})
    target_link_libraries(ft891-fieldtest PRIVATE rr_cat)
endif()

# ----------------------------------------------------------- installation
include(GNUInstallDirs)

if(FT891_BUILD_SERVER AND NOT ANDROID)
    install(TARGETS ft891remote-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(FT891_BUILD_CLIENT AND NOT ANDROID)
    install(TARGETS ft891remote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(NOT ANDROID)
    # The shortcuts carry @BINDIR@: the real install path goes in.
    set(BINDIR ${CMAKE_INSTALL_FULL_BINDIR})
    set(RR_DESKTOP_FILES "")
    if(FT891_BUILD_SERVER)
        list(APPEND RR_DESKTOP_FILES ft891remote-server)
    endif()
    if(FT891_BUILD_CLIENT)
        list(APPEND RR_DESKTOP_FILES ft891remote)
    endif()
    foreach(app IN LISTS RR_DESKTOP_FILES)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/desktop/${app}.desktop
                       ${CMAKE_CURRENT_BINARY_DIR}/${app}.desktop @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${app}.desktop
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    endforeach()

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons)

    if(UNIX AND NOT APPLE AND FT891_BUILD_SERVER)
        # A literal path, not CMAKE_INSTALL_LIBDIR: Debian qualifies that one
        # with the architecture, and systemd only looks in lib/systemd/user.
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/ft891remote-server.service
                DESTINATION lib/systemd/user)
    endif()
    if(UNIX AND NOT APPLE)
        # AppStream metadata: the launcher and the programs are those this
        # build installs.
        set(RR_METAINFO_BINARIES "")
        if(FT891_BUILD_CLIENT)
            set(RR_METAINFO_LAUNCHABLE ft891remote.desktop)
            string(APPEND RR_METAINFO_BINARIES "    <binary>ft891remote</binary>\n")
        else()
            set(RR_METAINFO_LAUNCHABLE ft891remote-server.desktop)
        endif()
        if(FT891_BUILD_SERVER)
            string(APPEND RR_METAINFO_BINARIES "    <binary>ft891remote-server</binary>\n")
        endif()
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/packaging/appstream/org.ft891remote.FT891Remote.metainfo.xml.in
                       ${CMAKE_CURRENT_BINARY_DIR}/org.ft891remote.FT891Remote.metainfo.xml @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.ft891remote.FT891Remote.metainfo.xml
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
    endif()

    # Debian wants the package name, lower case, not the project's.
    install(FILES README.md LICENSE.txt CHANGELOG.md data/cat/ft891.json
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/ft891remote)

    if(UNIX AND NOT APPLE)
        # Debian requires a copyright file and a compressed changelog. A
        # native package's changelog is changelog.gz, without ".Debian". The
        # changelog is compressed at configure time; bump_version.sh edits
        # CMakeLists.txt, which triggers a new configure.
        set(RR_CHANGELOG_GZ ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz)
        execute_process(
            COMMAND gzip -9 -n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/changelog
            OUTPUT_FILE ${RR_CHANGELOG_GZ})
        install(FILES ${RR_CHANGELOG_GZ} ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/copyright
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/ft891remote)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/changelog)

        # Manual pages, compressed as Debian expects them.
        set(RR_MAN_PAGES "")
        if(FT891_BUILD_SERVER)
            list(APPEND RR_MAN_PAGES ft891remote-server)
        endif()
        if(FT891_BUILD_CLIENT)
            list(APPEND RR_MAN_PAGES ft891remote)
        endif()
        foreach(page IN LISTS RR_MAN_PAGES)
            execute_process(
                COMMAND gzip -9 -n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man/${page}.1
                OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${page}.1.gz)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${page}.1.gz
                    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                         ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man/${page}.1)
        endforeach()
    endif()
endif()

# ---------------------------------------------------------- Debian package
# Dependencies are not written by hand: dpkg-shlibdeps derives them from the
# libraries actually linked, which gives the right list on Ubuntu as on
# Raspberry Pi OS.
if(NOT ANDROID)
    set(CPACK_PACKAGE_NAME "ft891remote")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "FT891Remote Community <noreply@ft891remote.invalid>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Remote operation of a Yaesu FT-891: audio, PTT and full CAT control")
    set(CPACK_PACKAGE_DESCRIPTION
"Operate a Yaesu FT-891 over the network, without Hamlib. A server runs next\\n\\
 to the radio and shares its audio, PTT and every setting reachable over CAT;\\n\\
 a Qt Quick client runs on the desktop or on Android.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/")
    # Unstripped binaries are three times their useful size.
    set(CPACK_STRIP_FILES ON)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire | pulseaudio")
    # QML modules are loaded at run time, never linked: dpkg-shlibdeps cannot
    # see them. Without them the client opens an empty window.
    if(FT891_BUILD_CLIENT)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript")
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst)
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm")
    endif()
    include(CPack)
endif()
