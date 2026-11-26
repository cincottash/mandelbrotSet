cmake_minimum_required(VERSION 3.21)

# The app's name lives here (and nowhere else): rename it in one place.
set(APP_NAME "mandelbrotSet")
set(APP_TITLE "Mandelbrot Set")
set(APP_ORG "cincottash")
project(${APP_NAME} VERSION 0.1.0 LANGUAGES CXX)
# Release builds pass the real version (e.g. -DRELEASE_VERSION=1.2.3); local builds show 0.1.0-dev.
set(RELEASE_VERSION "" CACHE STRING "Version shown in the app (set by the release workflow)")
if(RELEASE_VERSION)
    set(APP_VERSION "${RELEASE_VERSION}")
else()
    set(APP_VERSION "${PROJECT_VERSION}-dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# STATIC_BUILD=ON (with a static Qt) makes one self-contained exe: Qt and every library it uses are linked in,
# so nothing has to be installed next to it. Development builds leave it OFF for faster linking.
option(STATIC_BUILD "Link everything statically (release exe)" OFF)
if(STATIC_BUILD AND MINGW)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)          # prefer libfoo.a over the DLL import library libfoo.dll.a
    set(PKG_CONFIG_USE_STATIC_LIBS ON)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    # Libraries the static Qt dependencies (OpenSSL's libcrypto, FreeType) need but don't declare. They must come
    # last on the link line, after the libraries that use them.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lbz2 -lbrotlidec -lbrotlicommon -lws2_32 -lcrypt32 -liphlpapi -lbcrypt")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg OpenGL OpenGLWidgets Concurrent)
if(STATIC_BUILD AND MINGW AND TARGET harfbuzz::harfbuzz)
    # harfbuzz's CMake package always points at its DLL; use the static library instead.
    find_library(HARFBUZZ_STATIC NAMES libharfbuzz.a REQUIRED)
    set_target_properties(harfbuzz::harfbuzz PROPERTIES IMPORTED_LOCATION "${HARFBUZZ_STATIC}" IMPORTED_IMPLIB "${HARFBUZZ_STATIC}")
    # ...and everything the static harfbuzz needs (graphite2, glib, ...), as its pkg-config file lists them.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HB_STATIC REQUIRED harfbuzz)
    foreach(lib IN LISTS HB_STATIC_STATIC_LIBRARIES)
        string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -l${lib}")
    endforeach()
endif()
qt_standard_project_setup()

# Arbitrary-precision math for the view's center and the reference orbit.
find_path(MPFR_INCLUDE_DIR mpfr.h REQUIRED)
find_library(MPFR_LIBRARY NAMES mpfr REQUIRED)
find_library(GMP_LIBRARY NAMES gmp REQUIRED)
# Animated WebP for zoom movies.
find_library(WEBPMUX_LIBRARY NAMES webpmux REQUIRED)
find_library(WEBP_LIBRARY NAMES webp REQUIRED)
find_library(SHARPYUV_LIBRARY NAMES sharpyuv)
set(WEBP_LIBRARIES ${WEBPMUX_LIBRARY} ${WEBP_LIBRARY})
if(SHARPYUV_LIBRARY)
    list(APPEND WEBP_LIBRARIES ${SHARPYUV_LIBRARY})
endif()

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/render/*.cpp)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)

# The engine (no windows) is a library of its own, so the local tests can link the same code.
add_library(engine STATIC ${CORE_SOURCES})
target_include_directories(engine PUBLIC src ${MPFR_INCLUDE_DIR})
target_link_libraries(engine PUBLIC Qt6::Gui Qt6::OpenGL ${MPFR_LIBRARY} ${GMP_LIBRARY})
target_compile_definitions(engine PUBLIC
    APP_NAME="${APP_NAME}" APP_TITLE="${APP_TITLE}" APP_ORG="${APP_ORG}" APP_VERSION="${APP_VERSION}")
qt_add_resources(engine shaders PREFIX "/shaders" BASE src/render/shaders FILES
    src/render/shaders/iterate.comp src/render/shaders/colorize.frag src/render/shaders/display.frag
    src/render/shaders/fullscreen.vert src/render/shaders/boxes.comp src/render/shaders/bloom.frag
    src/render/shaders/sphere.frag src/render/shaders/resample.comp src/render/shaders/buddha.comp
    src/render/shaders/buddha.frag)

set(SOURCES ${APP_SOURCES})
if(WIN32)
    list(APPEND SOURCES packaging/app.rc)
    # The build doesn't see that app.rc uses icon.ico: without this, a new icon keeps the old one in the exe.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.ico)
        set_source_files_properties(packaging/app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.ico)
    endif()
endif()

qt_add_executable(app WIN32 ${SOURCES})
set_target_properties(app PROPERTIES OUTPUT_NAME ${APP_NAME})
target_include_directories(app PRIVATE src)
target_link_libraries(app PRIVATE engine Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Concurrent ${WEBP_LIBRARIES})

if(STATIC_BUILD)
    # Only the image plugins the app uses (SVG for icons and style-sheet images).
    qt_import_plugins(app
        INCLUDE_BY_TYPE imageformats Qt6::QSvgPlugin
        INCLUDE_BY_TYPE iconengines Qt6::QSvgIconPlugin)
    if(WIN32)
        # The normal Windows display plugin, plus "offscreen" for invisible tests (QT_QPA_PLATFORM=offscreen).
        qt_import_plugins(app INCLUDE_BY_TYPE platforms Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
    endif()
endif()

qt_add_resources(app resources PREFIX "/" BASE resources FILES
    resources/check.svg resources/check_dark.svg resources/icon.svg
    resources/down_light.svg resources/down_dark.svg resources/up_light.svg resources/up_dark.svg)

if(WIN32)
    # Media Foundation: Windows' own H.264 encoder for MP4 zoom movies.
    target_link_libraries(app PRIVATE user32 dwmapi mfplat mfreadwrite mfuuid ole32)
    target_compile_definitions(app PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        # No compiler runtime DLLs next to the exe; strip debug symbols from release builds.
        target_link_options(app PRIVATE -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(app PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS app RUNTIME DESTINATION bin)
