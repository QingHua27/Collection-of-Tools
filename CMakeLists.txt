cmake_minimum_required(VERSION 3.24)
project(CppToolbox VERSION 1.1.0 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/utf-8 /W3 /EHa)
set(S src/modules/screenshot)
set(R src/modules/recorder)
set(K src/modules/keepalive)
set(L src/modules/lol)
set_property(SOURCE assets/toolbox.rc PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/toolbox.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/tank.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/tank-paused.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/lol.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/zh-CN.ini;${CMAKE_CURRENT_SOURCE_DIR}/assets/en-US.ini;${CMAKE_CURRENT_SOURCE_DIR}/assets/alarm.wav;${CMAKE_CURRENT_SOURCE_DIR}/assets/record-shutter.wav;${CMAKE_CURRENT_SOURCE_DIR}/assets/lol-helper.lua;${CMAKE_CURRENT_SOURCE_DIR}/assets/botanical-paper-reference.png")
add_executable(CppToolbox WIN32
  src/main.cpp src/theme.cpp src/gateway.cpp src/ace.cpp src/ace_governor.cpp src/preview.cpp
  src/screenshot_module.cpp src/recording_module.cpp src/countdown_module.cpp
  src/keepalive_module.cpp src/league_module.cpp assets/toolbox.rc
  ${S}/common.cpp ${S}/capture.cpp ${S}/annotate.cpp ${S}/overlay.cpp ${S}/ocr.cpp
  ${S}/translate.cpp ${S}/settings.cpp ${S}/pin.cpp ${S}/output.cpp
  ${R}/common.cpp ${R}/recorder.cpp ${R}/wincap.cpp ${R}/history.cpp ${R}/folder_videos.cpp ${R}/player.cpp
  ${K}/keepalive.cpp ${K}/startup.cpp ${L}/attack_speed_exporter.cpp
)
target_link_options(CppToolbox PRIVATE /MANIFEST:EMBED /MANIFESTUAC:level='asInvoker')
target_link_libraries(CppToolbox PRIVATE
  user32 gdi32 shell32 comdlg32 advapi32 gdiplus ole32 oleaut32 winhttp
  dwmapi shcore windowsapp d3d11 dxgi comctl32 shlwapi winmm
  mfplay mfplat mfreadwrite mfuuid uxtheme
)
include(CTest)
if(BUILD_TESTING)
  add_executable(AlarmTimeTests tests/alarm_time_tests.cpp)
  add_test(NAME AlarmTimeTests COMMAND AlarmTimeTests)
  add_executable(FolderVideoTests tests/folder_videos_tests.cpp ${R}/folder_videos.cpp)
  target_include_directories(FolderVideoTests PRIVATE ${R})
  add_test(NAME FolderVideoTests COMMAND FolderVideoTests)
  add_executable(AceGovernorTests tests/ace_governor_tests.cpp src/ace_governor.cpp)
  target_include_directories(AceGovernorTests PRIVATE src)
  target_link_libraries(AceGovernorTests PRIVATE advapi32)
  add_test(NAME AceGovernorTests COMMAND AceGovernorTests)
  add_executable(AceLiveProbe tests/ace_live_probe.cpp src/ace_governor.cpp)
  target_include_directories(AceLiveProbe PRIVATE src)
  target_link_libraries(AceLiveProbe PRIVATE advapi32)
  add_executable(KeepAliveTests tests/keepalive_tests.cpp ${K}/keepalive.cpp)
  target_include_directories(KeepAliveTests PRIVATE ${K})
  target_link_libraries(KeepAliveTests PRIVATE user32)
  add_test(NAME KeepAliveTests COMMAND KeepAliveTests)
  add_executable(StartupTests tests/startup_tests.cpp ${K}/startup.cpp)
  target_include_directories(StartupTests PRIVATE ${K})
  target_link_libraries(StartupTests PRIVATE advapi32)
  add_test(NAME StartupTests COMMAND StartupTests)
  add_executable(AttackSpeedTests ${L}/attack_speed_exporter.cpp ${L}/attack_speed_exporter_tests.cpp)
  target_include_directories(AttackSpeedTests PRIVATE ${L})
  target_link_libraries(AttackSpeedTests PRIVATE winhttp)
  add_test(NAME AttackSpeedTests COMMAND AttackSpeedTests)
  add_executable(HotkeyTests tests/hotkey_gate_test.cpp)
  target_include_directories(HotkeyTests PRIVATE ${R})
  target_link_libraries(HotkeyTests PRIVATE user32)
  add_test(NAME HotkeyTests COMMAND HotkeyTests)
endif()
