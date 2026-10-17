cmake_minimum_required(VERSION 3.14)
project(StudioKEYS VERSION 1.0.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(SK_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../host")
set(SK_FW_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/src")
set(SK_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/gen")

option(STUDIOKEYS_BUILD_APP "Build standalone APP (needs full Xcode.app ibtool on macOS)" OFF)
if(APPLE AND NOT STUDIOKEYS_BUILD_APP)
  # CLT ships a stub ibtool that answers --help but cannot compile nibs.
  # Only auto-enable when a real Xcode.app is present.
  if(EXISTS "/Applications/Xcode.app/Contents/Developer/usr/bin/ibtool")
    set(STUDIOKEYS_BUILD_APP ON CACHE BOOL "Build standalone APP (needs full Xcode.app ibtool on macOS)" FORCE)
  endif()
endif()

set(STUDIOKEYS_SOURCES
  StudioKEYS.cpp
  StudioKEYS.h
  MidiCables.h
  HostEngine.h
  LockSession.h
  Librarian.h
  ImportBay.h
  DeskParams.h
  PageInspector.h
  MotionWindow.h
  SampleSlotBay.h
  SkWav.h
  SurfaceCard.h
  UsbMidiLink.h
  BayDrag.h
  PanelLayout.h
  SkMint.h
  SkPanelControls.h
  Fm6Desk.h
  EngineDeskUI.h
  SettingsDesk.h
  AnalogDesk.h
  WheelDesk.h
  LofiDesk.h
  SampleDesk.h
  VoiceDesk.h
  TrioDesk.h
  GrainDesk.h
  PhysDesk.h
  NoiseDesk.h
  SliceDesk.h
  DrumDesk.h
  PatchOverlays.h
  resources/resource.h
  resources/fonts/RobotoRegular.c
  resources/fonts/RobotoRegular.h
)

set(STUDIOKEYS_FORMATS VST3)
if(STUDIOKEYS_BUILD_APP)
  list(APPEND STUDIOKEYS_FORMATS APP)
  message(STATUS "StudioKEYS: APP format enabled")
else()
  message(STATUS "StudioKEYS: APP format skipped (no ibtool / STUDIOKEYS_BUILD_APP=OFF)")
endif()

set(STUDIOKEYS_LINK iPlug2::Extras::Synth)
set(STUDIOKEYS_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}")

if(EXISTS "${SK_HOST_DIR}/sk_host.c" AND EXISTS "${SK_HOST_DIR}/sk_host.h")
  message(STATUS "StudioKEYS: linking native host from ${SK_HOST_DIR}")
  add_library(sk_host STATIC "${SK_HOST_DIR}/sk_host.c")
  set_target_properties(sk_host PROPERTIES LINKER_LANGUAGE C)
  target_include_directories(sk_host PUBLIC
    "${SK_HOST_DIR}"
    "${SK_FW_SRC}"
    "${SK_GEN_DIR}"
  )
  target_compile_options(sk_host PRIVATE -O2 -ffp-contract=off -w)
  target_compile_definitions(sk_host PRIVATE NVOICE=128)
  if(NOT WIN32)
    target_link_libraries(sk_host PUBLIC m)
  endif()
  list(APPEND STUDIOKEYS_LINK sk_host)
  list(APPEND STUDIOKEYS_INCLUDES "${SK_HOST_DIR}")
else()
  message(STATUS "StudioKEYS: host missing — building thin sk_host_stub.c")
  list(APPEND STUDIOKEYS_SOURCES sk_host_stub.c)
  # sk_host_stub.c includes sk_host.h from a vendored copy beside the stub.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sk_host_stub_api.h")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sk_host_stub_api.h"
                   "${CMAKE_CURRENT_BINARY_DIR}/sk_host.h" COPYONLY)
    list(APPEND STUDIOKEYS_INCLUDES "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()

# SKL/SKS SysEx constants (also available when only the stub is linked).
if(EXISTS "${SK_HOST_DIR}/sk_skl.h")
  configure_file("${SK_HOST_DIR}/sk_skl.h" "${CMAKE_CURRENT_BINARY_DIR}/sk_skl.h" COPYONLY)
  list(APPEND STUDIOKEYS_INCLUDES "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(EXISTS "${SK_HOST_DIR}/sk_sks.h")
  configure_file("${SK_HOST_DIR}/sk_sks.h" "${CMAKE_CURRENT_BINARY_DIR}/sk_sks.h" COPYONLY)
  list(APPEND STUDIOKEYS_INCLUDES "${CMAKE_CURRENT_BINARY_DIR}")
endif()

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    ${STUDIOKEYS_SOURCES}
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    ${STUDIOKEYS_FORMATS}
  LINK
    ${STUDIOKEYS_LINK}
)

foreach(_tgt ${PROJECT_NAME} ${PROJECT_NAME}-vst3 ${PROJECT_NAME}-app ${PROJECT_NAME}-object)
  if(TARGET ${_tgt})
    target_include_directories(${_tgt} PRIVATE ${STUDIOKEYS_INCLUDES})
    if(APPLE)
      target_link_libraries(${_tgt} PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
    endif()
  endif()
endforeach()

# Linker-adhoc sign omits Resources; Ableton then fails with
# "code has no resources but signature indicates they must be present".
# iPlug copies to ~/Library after link — resign BOTH the build out bundle and the
# installed copy (copy runs before this POST_BUILD in practice on some gens).
if(APPLE)
  set(_SK_VST3_INSTALL "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/StudioKEYS.vst3")
  if(TARGET ${PROJECT_NAME}-vst3)
    add_custom_command(TARGET ${PROJECT_NAME}-vst3 POST_BUILD
      COMMAND codesign --force --deep --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}-vst3>"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}-vst3>" "${_SK_VST3_INSTALL}"
      COMMAND codesign --force --deep --sign - --timestamp=none "${_SK_VST3_INSTALL}"
      COMMAND ${CMAKE_COMMAND} -E echo "[StudioKEYS] codesign+install OK ${_SK_VST3_INSTALL}"
      VERBATIM)
  endif()
  if(TARGET ${PROJECT_NAME}-app)
    add_custom_command(TARGET ${PROJECT_NAME}-app POST_BUILD
      COMMAND codesign --force --deep --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}-app>"
      COMMAND ${CMAKE_COMMAND} -E echo "[StudioKEYS] codesign adhoc OK $<TARGET_BUNDLE_DIR:${PROJECT_NAME}-app>"
      VERBATIM)
  endif()
endif()

# Keep PageInspector.h in sync with firmware PAGES[] when regenerating.
add_custom_target(sk_pages_gen
  COMMAND ${CMAKE_COMMAND} -E env python3 "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_page_inspector.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  COMMENT "Regenerate PageInspector.h from firmware PAGES[]"
)
