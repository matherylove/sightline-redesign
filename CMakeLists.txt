cmake_minimum_required(VERSION 2.8)
project(SightlinePlayer)

# ── Source files ─────────────────────────────────────────────
# You need Dear ImGui source tree in ./imgui/
# Grab from: https://github.com/ocornut/imgui (tag v1.90.x recommended)
# Put these files in ./imgui/:
#   imgui.cpp, imgui.h, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
#   imgui_internal.h, imconfig.h, imstb_rectpack.h, imstb_textedit.h, imstb_truetype.h
#   backends/imgui_impl_win32.cpp, backends/imgui_impl_win32.h
#   backends/imgui_impl_dx9.cpp,  backends/imgui_impl_dx9.h

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui")

# ── FFmpeg XP-mod ─────────────────────────────────────────────
# Extract ffmpeg-7.1-*-win32-dev-xpmod-sse.7z into ./ffmpeg-xp/
# The layout must be:
#   ffmpeg-xp/include/libavformat/avformat.h  (and other headers)
#   ffmpeg-xp/lib/libavcodec.a  (and other static libs)
# You can override FFMPEG_DIR on the cmake command line:
#   cmake -DFFMPEG_DIR=C:/path/to/ffmpeg-xp ...
if(NOT DEFINED FFMPEG_DIR)
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/ffmpeg-xp")
endif()

set(SOURCES
    src/main.cpp
    src/video_player.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
)

include_directories(
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FFMPEG_DIR}/include
)

add_executable(SightlinePlayer WIN32 ${SOURCES})

target_link_directories(SightlinePlayer PRIVATE ${FFMPEG_DIR}/lib)

# uuid     — WIC GUIDs (IID_IWICImagingFactory, CLSID_WICImagingFactory, GUID_WICPixelFormat32bppBGRA)
# ole32    — CoCreateInstance / CoInitializeEx
# oleaut32 — OLE Automation (COM helpers)
# winmm    — waveOut audio API (available on XP SP3)
# FFmpeg libs: order matters — higher-level first, lower-level last
target_link_libraries(SightlinePlayer
    d3d9 dwmapi user32 gdi32 gdiplus shell32 ole32 oleaut32 uuid winmm
    avcodec avformat avutil swscale swresample
)

if(MSVC)
    set_target_properties(SightlinePlayer PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS,5.01"
    )
    target_compile_definitions(SightlinePlayer PRIVATE
        WINVER=0x0501 _WIN32_WINNT=0x0501
        _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    )
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if(${flag_var} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
      endif()
    endforeach()
elseif(MINGW)
    set_target_properties(SightlinePlayer PROPERTIES
        LINK_FLAGS "-mwindows -m32 -static -static-libgcc -static-libstdc++"
    )
    target_compile_definitions(SightlinePlayer PRIVATE
        WINVER=0x0501 _WIN32_WINNT=0x0501
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(SightlinePlayer PRIVATE -m32 -O2)
endif()
