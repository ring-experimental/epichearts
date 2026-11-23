cmake_minimum_required(VERSION 3.24)
project(EpicHearts C CXX)

# 32-bit hosts use guest pointers as host pointers; 64-bit hosts map the guest into one reserved region (src/gas.h)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# ---- web (Emscripten): threads for the game and the original's own threads (SharedArrayBuffer; the page's service
# worker makes it cross-origin isolated, web/sw.js)
if(EMSCRIPTEN)
  add_compile_options(-pthread)
  add_link_options(-pthread)
  set(SDL_PTHREADS ON CACHE BOOL "" FORCE) # off by default on Emscripten: SDL_CreateThread would fail
endif()

# ---- third party
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
add_subdirectory(third_party/rmlui EXCLUDE_FROM_ALL)

# ---- the original's code
# EH_INTERP=OFF: recompiled to C (src/gen, made from the original by tools/recomp.py; never published)
# EH_INTERP=ON : interpreted at run time from the original the player supplies (src/interp.c; the web build)
option(EH_INTERP "run the original's code in the interpreter instead of the recompiled src/gen" OFF)
if(EMSCRIPTEN)
  set(EH_INTERP ON CACHE BOOL "" FORCE)
endif()
if(NOT EH_INTERP)
  file(GLOB GEN_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/gen_*.c)
  add_library(gen STATIC ${GEN_SRC})
  target_include_directories(gen PRIVATE src)
  if(MSVC)
    # generated code: huge functions, many labels
    target_compile_options(gen PRIVATE /O1 /bigobj /wd4102 /wd4101 /wd4189 /wd4244 /W1)
  else()
    target_compile_options(gen PRIVATE -O1 -w -fno-strict-aliasing -fwrapv)
  endif()
endif()

# ---- host (platform layer + modern UI)
set(HOST_SRC
  src/main.c src/loader.c src/libc.c src/gl.c src/jni.c src/java.c src/java_font.c src/java_audio.c src/java_net.c
  src/apk.c src/hooks.c src/java_text.c src/java_alert.c src/bot.c src/platform.c src/textconv.c src/gas.c
  src/ui/render.cpp src/ui/ui.cpp src/ui/gamestate.cpp src/ui/capture.cpp src/ui/input.cpp src/ui/screens.cpp src/ui/state.cpp src/ui/sprites.cpp src/ui/title.cpp src/ui/saveio.cpp src/ui/autoform.cpp src/ui/autoscreen.cpp src/ui/settings.cpp src/ui/options.cpp src/ui/menu.cpp src/ui/menu_pages.cpp src/ui/dialogs.cpp src/ui/charselect.cpp src/ui/town.cpp src/ui/store.cpp src/ui/map.cpp src/ui/helpfull.cpp src/ui/cashshop.cpp src/ui/bindings.cpp src/ui/keymap.cpp
  src/ui/talkctl.cpp src/ui/help.cpp src/ui/bagbuy.cpp src/ui/qbox.cpp src/ui/smithy.cpp src/ui/farm.cpp src/ui/skillbook.cpp src/ui/detail.cpp src/ui/collection.cpp src/ui/hotslot.cpp
  third_party/rmlui/Backends/RmlUi_Renderer_GL3.cpp third_party/rmlui/Backends/RmlUi_Platform_SDL.cpp)

if(EMSCRIPTEN)
  # the page (web/) loads epichearts.js / .wasm / .data; main runs when the player's APK has been handed over
  add_executable(epichearts ${HOST_SRC})
  set(HOST_TARGET epichearts)
  target_compile_definitions(epichearts PRIVATE RMLUI_PLATFORM_EMSCRIPTEN)
  target_link_options(epichearts PRIVATE
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648
    -sSTACK_SIZE=4194304 -sDEFAULT_PTHREAD_STACK_SIZE=4194304 -sPTHREAD_POOL_SIZE=6
    -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -lidbfs.js
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_eh_set_apk,_eh_save_picked
    -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,HEAPU8,ENV,UTF8ToString,stringToNewUTF8
    -sENVIRONMENT=web,worker -sMODULARIZE=0 --profiling-funcs
    --preload-file ${CMAKE_SOURCE_DIR}/data/ui@/data/ui)
elseif(ANDROID)
  add_library(main SHARED ${HOST_SRC})
  set(HOST_TARGET main)
  target_link_libraries(main PRIVATE GLESv3 EGL log android mediandk)
  target_link_options(main PRIVATE -Wl,--version-script=${CMAKE_SOURCE_DIR}/android/exports.map -Wl,--gc-sections)
else()
  add_executable(epichearts WIN32 ${HOST_SRC} src/app.rc)
  set(HOST_TARGET epichearts)
  target_link_libraries(epichearts PRIVATE dbghelp mfplat mfreadwrite mfuuid shlwapi ole32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(epichearts PROPERTIES LINK_FLAGS "/LARGEADDRESSAWARE:NO /STACK:16777216")
  else()
    set_target_properties(epichearts PROPERTIES LINK_FLAGS "/STACK:16777216")
  endif()
endif()
target_include_directories(${HOST_TARGET} PRIVATE src third_party/stb third_party/rmlui/Backends)
target_compile_definitions(${HOST_TARGET} PRIVATE RMLUI_SDL_VERSION_MAJOR=3 RMLUI_STATIC_LIB)
if(MSVC)
  target_compile_options(${HOST_TARGET} PRIVATE $<$<COMPILE_LANGUAGE:C>:/W3 /wd4996> /utf-8)
  target_compile_definitions(${HOST_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(${HOST_TARGET} PRIVATE -fno-strict-aliasing -fwrapv -Wno-unused-parameter)
endif()
if(EH_INTERP)
  target_sources(${HOST_TARGET} PRIVATE src/interp.c src/accel.c)
  target_compile_definitions(${HOST_TARGET} PRIVATE EH_INTERP)
else()
  target_link_libraries(${HOST_TARGET} PRIVATE gen)
endif()
target_link_libraries(${HOST_TARGET} PRIVATE SDL3::SDL3-static RmlUi::RmlUi RmlUi::Debugger freetype)
