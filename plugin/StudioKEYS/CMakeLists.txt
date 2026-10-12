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

set(STUDIOKEYS_SOURCES
  StudioKEYS.cpp
  StudioKEYS.h
  MidiCables.h
  HostEngine.h
  LockSession.h
  Librarian.h
  ImportBay.h
  DeskParams.h
  SurfaceCard.h
  SkMint.h
  SkPanelControls.h
  resources/resource.h
)

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

# SKL lock SysEx constants (also available when only the stub is linked).
if(EXISTS "${SK_HOST_DIR}/sk_skl.h")
  configure_file("${SK_HOST_DIR}/sk_skl.h" "${CMAKE_CURRENT_BINARY_DIR}/sk_skl.h" COPYONLY)
  list(APPEND STUDIOKEYS_INCLUDES "${CMAKE_CURRENT_BINARY_DIR}")
endif()

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    ${STUDIOKEYS_SOURCES}
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    VST3
    APP
  LINK
    ${STUDIOKEYS_LINK}
)

foreach(_tgt ${PROJECT_NAME} ${PROJECT_NAME}-vst3 ${PROJECT_NAME}-app ${PROJECT_NAME}-object)
  if(TARGET ${_tgt})
    target_include_directories(${_tgt} PRIVATE ${STUDIOKEYS_INCLUDES})
  endif()
endforeach()
