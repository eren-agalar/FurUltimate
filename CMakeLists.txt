cmake_minimum_required(VERSION 3.20)
project(FurUltimate VERSION 1.3.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT on MSVC so the executable has no redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT WIN32)
  message(FATAL_ERROR "FurUltimate targets Windows. Use MSVC/MinGW on Windows or cmake/toolchain-mingw64.cmake to cross-compile.")
endif()

set(AETHER_VERSION_STRING "${PROJECT_VERSION}")

# --- Embedded shaders --------------------------------------------------------------
file(GLOB SHADER_FILES ${CMAKE_SOURCE_DIR}/src/render/shaders/*.hlsl ${CMAKE_SOURCE_DIR}/src/render/shaders/*.hlsli)
set(EMBED_OUT ${CMAKE_BINARY_DIR}/generated/EmbeddedShaders.h)
add_custom_command(
  OUTPUT ${EMBED_OUT}
  COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR}/src/render/shaders -DOUT=${EMBED_OUT} -P ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
  DEPENDS ${SHADER_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
  COMMENT "Embedding HLSL shaders")
add_custom_target(aether_shaders DEPENDS ${EMBED_OUT})

# --- Third party -------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
if(MINGW)
  target_compile_options(miniz PRIVATE -O2 -w)
endif()
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
if(MINGW)
  target_compile_options(sqlite3 PRIVATE -O2 -w)
endif()

# --- Engine library (everything except the UI shell) --------------------------------
set(ENGINE_SOURCES
  src/core/Log.cpp src/core/Strings.cpp src/core/Paths.cpp src/core/FileIO.cpp src/core/Settings.cpp
  src/core/I18n.cpp src/core/Image.cpp
  src/gpu/GpuDevice.cpp src/gpu/ShaderLibrary.cpp
  src/render/SceneRenderer.cpp src/render/RenderSurface.cpp
  src/telemetry/Telemetry.cpp src/telemetry/NvmlProvider.cpp src/telemetry/AdlProvider.cpp
  src/telemetry/D3dkmtProvider.cpp src/telemetry/PdhProvider.cpp
  src/hw/HardwareInfo.cpp
  src/data/Result.cpp src/data/Database.cpp
  src/stress/StressEngine.cpp src/bench/BenchmarkEngine.cpp src/bench/AiBench.cpp
  src/report/PdfWriter.cpp src/report/ReportExporter.cpp src/net/Leaderboard.cpp src/net/Updater.cpp
  src/ui/Fonts.cpp src/ui/Theme.cpp
)
add_library(aether_engine STATIC ${ENGINE_SOURCES})
add_dependencies(aether_engine aether_shaders)
target_include_directories(aether_engine PUBLIC src third_party/nlohmann ${CMAKE_BINARY_DIR}/generated)

# Release builds embed the online leaderboard endpoint (public anon key) from an untracked leaderboard.json,
# so the standalone .exe works without a side file. Source builds without it simply have no leaderboard.
set(FUL_LB_URL "")
set(FUL_LB_KEY "")
if(EXISTS ${CMAKE_SOURCE_DIR}/leaderboard.json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/leaderboard.json)
  file(READ ${CMAKE_SOURCE_DIR}/leaderboard.json FUL_LB_JSON)
  string(JSON FUL_LB_URL ERROR_VARIABLE FUL_LB_E1 GET "${FUL_LB_JSON}" url)
  string(JSON FUL_LB_KEY ERROR_VARIABLE FUL_LB_E2 GET "${FUL_LB_JSON}" anonKey)
  if(FUL_LB_E1 OR FUL_LB_E2)
    set(FUL_LB_URL "")
    set(FUL_LB_KEY "")
  endif()
endif()
configure_file(src/net/LeaderboardEmbedded.h.in ${CMAKE_BINARY_DIR}/generated/LeaderboardEmbedded.h @ONLY)
target_include_directories(aether_engine PUBLIC third_party third_party/directml)
target_compile_definitions(aether_engine PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00
  AETHER_VERSION="${AETHER_VERSION_STRING}")
target_link_libraries(aether_engine PUBLIC sqlite3 miniz winhttp bcrypt d3d11 dxgi d2d1 dwrite windowscodecs pdh setupapi ole32 uuid shell32 user32 gdi32 comdlg32 shlwapi dwmapi)
if(MSVC)
  target_compile_options(aether_engine PUBLIC /utf-8 /W3 /permissive- /EHsc)
  target_compile_definitions(aether_engine PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()
if(MINGW)
  target_compile_options(aether_engine PUBLIC -Wall -Wno-unknown-pragmas -Wno-format-security -Wno-missing-field-initializers -Wno-cast-function-type)
  target_link_options(aether_engine PUBLIC -static -static-libgcc -static-libstdc++)
endif()

# --- Application -------------------------------------------------------------------
if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
  file(GLOB UI_SOURCES src/ui/*.cpp src/ui/pages/*.cpp)
  list(REMOVE_ITEM UI_SOURCES ${CMAKE_SOURCE_DIR}/src/ui/Fonts.cpp ${CMAKE_SOURCE_DIR}/src/ui/Theme.cpp)
  configure_file(src/app.rc.in ${CMAKE_BINARY_DIR}/generated/app.rc @ONLY)
  add_executable(FurUltimate WIN32 src/main.cpp ${UI_SOURCES} ${CMAKE_BINARY_DIR}/generated/app.rc)
  target_include_directories(FurUltimate PRIVATE src)
  target_link_libraries(FurUltimate PRIVATE aether_engine)
  set_target_properties(FurUltimate PROPERTIES OUTPUT_NAME "FurUltimate")
  if(MINGW)
    target_link_options(FurUltimate PRIVATE -municode)
  endif()
endif()

# --- Tests -------------------------------------------------------------------------
option(AETHER_BUILD_TESTS "Build unit / integration tests" ON)
if(AETHER_BUILD_TESTS AND EXISTS ${CMAKE_SOURCE_DIR}/tests/tests.cpp)
  add_executable(aether_tests tests/tests.cpp ${CMAKE_BINARY_DIR}/generated/app.rc)
  if(MINGW)
    target_link_options(aether_tests PRIVATE -municode)
  endif()
  target_link_libraries(aether_tests PRIVATE aether_engine)
endif()
