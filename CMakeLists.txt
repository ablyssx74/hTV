# hTV Linux build (SDL2 + libmpv playback, Qt6 widgets for the Config UI).
#
# The Haiku build still uses the Makefile/hTV.rdef/hTV.tpl in this directory
# and is untouched by this file. On Linux/CachyOS (or any distro):
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/hTV <url-or-file>
#
# SDL2 auto-detects Wayland vs X11 (recent SDL2 defaults to Wayland under a
# Wayland session); Qt auto-detects its platform plugin the same way, as
# long as the qt6-wayland package is installed alongside qt6-base.

cmake_minimum_required(VERSION 3.16)
project(hTV LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_executable(hTV
    linux/hTV_linux.cpp
    audio_fx.cpp
    linux/linux_config_ui.cpp
)

target_include_directories(hTV PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/linux
)

target_compile_options(hTV PRIVATE -Wall)

target_link_libraries(hTV PRIVATE
    PkgConfig::SDL2
    PkgConfig::MPV
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::CURL
    Qt6::Widgets
    OpenGL::GL
    Threads::Threads
)

# Install rules -- used by `cmake --install build` directly, and by the
# PKGBUILD's package() step for `makepkg -si` on Arch/CachyOS.
include(GNUInstallDirs)

install(TARGETS hTV RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES linux/hTV.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
