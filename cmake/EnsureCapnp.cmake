function(qucs_ensure_capnp)
    # On Windows never reuse *capnp-install-linux* (ELF .a → undefined Cap'n Proto
    # refs when linking qucs-s). Prefer Door's MinGW prefix, then Room's native install.

    set(_force_rescan FALSE)
    if(CAPNP_ROOT)
        if(WIN32 AND CAPNP_ROOT MATCHES "linux")
            message(STATUS "Ignoring Linux Cap'n Proto prefix on Windows: ${CAPNP_ROOT}")
            set(_force_rescan TRUE)
        elseif(NOT EXISTS "${CAPNP_ROOT}/include/capnp/message.h")
            message(STATUS "Ignoring missing Cap'n Proto prefix: ${CAPNP_ROOT}")
            set(_force_rescan TRUE)
        endif()
    endif()

    # Stale find_library results from a previous Linux-prefix configure.
    if(WIN32 AND CAPNP_LIBRARY MATCHES "linux")
        message(STATUS "Clearing stale Cap'n Proto libs: ${CAPNP_LIBRARY}")
        set(_force_rescan TRUE)
    endif()

    if(_force_rescan)
        unset(CAPNP_ROOT CACHE)
        unset(CAPNP_LIBRARY CACHE)
        unset(KJ_LIBRARY CACHE)
        unset(CAPNP_EXECUTABLE CACHE)
        unset(CAPNPC_CXX_EXECUTABLE CACHE)
    elseif(CAPNP_ROOT AND EXISTS "${CAPNP_ROOT}/include/capnp/message.h")
        # Drop library cache entries that do not live under CAPNP_ROOT.
        if(CAPNP_LIBRARY AND NOT CAPNP_LIBRARY MATCHES "${CAPNP_ROOT}")
            unset(CAPNP_LIBRARY CACHE)
            unset(KJ_LIBRARY CACHE)
        endif()
        message(STATUS "Using Cap'n Proto from CAPNP_ROOT: ${CAPNP_ROOT}")
        return()
    endif()

    set(_candidates "")
    if(WIN32)
        foreach(_sibling_name IN ITEMS Door LibMan)
            list(APPEND _candidates
                "${CMAKE_SOURCE_DIR}/../${_sibling_name}/capnp-install")
        endforeach()
        list(APPEND _candidates
            "${CMAKE_SOURCE_DIR}/../Room/third_party/capnp-install"
            "${CMAKE_SOURCE_DIR}/../CommonDB/third_party/capnp-install"
            "${CMAKE_SOURCE_DIR}/capnp-install")
    else()
        list(APPEND _candidates
            "${CMAKE_SOURCE_DIR}/../Room/third_party/capnp-install-linux"
            "${CMAKE_SOURCE_DIR}/../Room/third_party/capnp-install"
            "${CMAKE_SOURCE_DIR}/../CommonDB/third_party/capnp-install-linux"
            "${CMAKE_SOURCE_DIR}/../CommonDB/third_party/capnp-install"
            "${CMAKE_SOURCE_DIR}/../Door/capnp-install"
            "${CMAKE_SOURCE_DIR}/capnp-install")
    endif()

    foreach(_capnp_root IN LISTS _candidates)
        if(EXISTS "${_capnp_root}/include/capnp/message.h")
            get_filename_component(_capnp_root "${_capnp_root}" ABSOLUTE)
            set(CAPNP_ROOT "${_capnp_root}" CACHE PATH "Cap'n Proto install prefix" FORCE)
            unset(CAPNP_LIBRARY CACHE)
            unset(KJ_LIBRARY CACHE)
            unset(CAPNP_EXECUTABLE CACHE)
            unset(CAPNPC_CXX_EXECUTABLE CACHE)
            message(STATUS "Using Cap'n Proto from ${_capnp_root}")
            return()
        endif()
    endforeach()

    message(FATAL_ERROR
        "Cap'n Proto not found. Set CAPNP_ROOT or reuse Door/Room capnp-install "
        "(../Door/capnp-install or ../Room/third_party/capnp-install).")
endfunction()
