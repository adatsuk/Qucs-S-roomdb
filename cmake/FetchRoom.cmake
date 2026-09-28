include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/EnsureCapnp.cmake")

option(QUCS_ENABLE_ROOM "Enable ROOM (.room) schematic import/export in Qucs-S" ON)
set(QUCS_ROOM_SOURCE_DIR ""
    CACHE PATH "Local CommonDB checkout (skips git fetch)")
set(QUCS_ROOM_GIT_URL "https://github.com/IHP-GmbH/Room.git"
    CACHE STRING "ROOM Git repository URL")
set(QUCS_ROOM_GIT_TAG "main"
    CACHE STRING "ROOM Git branch or tag")

function(_qucs_configure_room_subproject)
    qucs_ensure_capnp()
    set(ROOM_BOOTSTRAP_CAPNP OFF CACHE BOOL "ROOM uses external Cap'n Proto" FORCE)
    set(ROOM_BUILD_EXAMPLES OFF CACHE BOOL "Do not build ROOM CLI tools inside Qucs-S" FORCE)
endfunction()

function(_qucs_add_room_aliases)
    if(TARGET room AND NOT TARGET ROOM::room)
        add_library(ROOM::room ALIAS room)
    endif()
    if(TARGET room_utils AND NOT TARGET ROOM::room_utils)
        add_library(ROOM::room_utils ALIAS room_utils)
    endif()
endfunction()

if(NOT QUCS_ENABLE_ROOM)
    message(STATUS "ROOM support disabled (QUCS_ENABLE_ROOM=OFF)")
    return()
endif()

if(QUCS_ROOM_SOURCE_DIR)
    if(NOT IS_DIRECTORY "${QUCS_ROOM_SOURCE_DIR}")
        message(FATAL_ERROR "QUCS_ROOM_SOURCE_DIR is not a directory: ${QUCS_ROOM_SOURCE_DIR}")
    endif()
    message(STATUS "Using local ROOM tree: ${QUCS_ROOM_SOURCE_DIR}")
    _qucs_configure_room_subproject()
    add_subdirectory("${QUCS_ROOM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/commondb-build")
    _qucs_add_room_aliases()
else()
  set(_default_room "${CMAKE_SOURCE_DIR}/../CommonDB")
  if(IS_DIRECTORY "${_default_room}")
      set(QUCS_ROOM_SOURCE_DIR "${_default_room}")
      message(STATUS "Using sibling ROOM tree: ${QUCS_ROOM_SOURCE_DIR}")
      _qucs_configure_room_subproject()
      add_subdirectory("${QUCS_ROOM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/commondb-build")
      _qucs_add_room_aliases()
  else()
      _qucs_configure_room_subproject()
      FetchContent_Declare(
          commondb
          GIT_REPOSITORY "${QUCS_ROOM_GIT_URL}"
          GIT_TAG "${QUCS_ROOM_GIT_TAG}"
          GIT_SHALLOW TRUE
          SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/CommonDB"
          BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/commondb-build"
      )
      message(STATUS "Fetching ROOM from ${QUCS_ROOM_GIT_URL} (${QUCS_ROOM_GIT_TAG})...")
      FetchContent_MakeAvailable(commondb)
      _qucs_add_room_aliases()
  endif()
endif()

if(NOT TARGET ROOM::room)
    message(FATAL_ERROR "ROOM target ROOM::room is missing after configuration.")
endif()

message(STATUS "ROOM linked into Qucs-S")
