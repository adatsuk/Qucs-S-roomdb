function(qucs_ensure_capnp)
    if(CAPNP_ROOT AND EXISTS "${CAPNP_ROOT}/include/capnp/message.h")
        return()
    endif()

    foreach(_subdir IN ITEMS capnp-install-linux capnp-install)
        set(_sibling_room
            "${CMAKE_SOURCE_DIR}/../Room/third_party/${_subdir}/include/capnp/message.h")
        if(EXISTS "${_sibling_room}")
            get_filename_component(_capnp_root "${_sibling_room}/../../.." ABSOLUTE)
            set(CAPNP_ROOT "${_capnp_root}" CACHE PATH "Cap'n Proto install prefix" FORCE)
            message(STATUS "Using Cap'n Proto from Room: ${CAPNP_ROOT}")
            return()
        endif()
        set(_sibling_commondb
            "${CMAKE_SOURCE_DIR}/../CommonDB/third_party/${_subdir}/include/capnp/message.h")
        if(EXISTS "${_sibling_commondb}")
            get_filename_component(_capnp_root "${_sibling_commondb}/../../.." ABSOLUTE)
            set(CAPNP_ROOT "${_capnp_root}" CACHE PATH "Cap'n Proto install prefix" FORCE)
            message(STATUS "Using Cap'n Proto from CommonDB: ${CAPNP_ROOT}")
            return()
        endif()
    endforeach()

    # Door (formerly LibMan) ships a Windows Cap'n Proto prefix next to the checkout.
    foreach(_sibling_name IN ITEMS Door LibMan)
        set(_sibling_door
            "${CMAKE_SOURCE_DIR}/../${_sibling_name}/capnp-install/include/capnp/message.h")
        if(EXISTS "${_sibling_door}")
            get_filename_component(_capnp_root "${_sibling_door}/../../.." ABSOLUTE)
            set(CAPNP_ROOT "${_capnp_root}" CACHE PATH "Cap'n Proto install prefix" FORCE)
            message(STATUS "Using Cap'n Proto from ${_sibling_name}: ${CAPNP_ROOT}")
            return()
        endif()
    endforeach()

    set(_local_capnp "${CMAKE_SOURCE_DIR}/capnp-install/include/capnp/message.h")
    if(EXISTS "${_local_capnp}")
        set(CAPNP_ROOT "${CMAKE_SOURCE_DIR}/capnp-install" CACHE PATH "Cap'n Proto install prefix" FORCE)
        return()
    endif()

    message(FATAL_ERROR
        "Cap'n Proto not found. Set CAPNP_ROOT or reuse Door/Room capnp-install "
        "(../Door/capnp-install or ../Room/third_party/capnp-install).")
endfunction()
