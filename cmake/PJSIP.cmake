if(TARGET PJSIP::pjsua2)
    return()
endif()

set(_PJSIP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/pjproject")
get_filename_component(_PJSIP_ROOT "${_PJSIP_ROOT}" ABSOLUTE)
if(NOT EXISTS "${_PJSIP_ROOT}/pjproject-vs14.sln")
    message(FATAL_ERROR "Submodule pjproject 2.17 não inicializado. Rode 'git submodule update --init --recursive' primeiro.")
endif()

set(_PJSIP_EXPECTED_LIBRARIES
    pjsua2-lib pjsua-lib pjsip-ua pjsip-simple pjsip-core
    pjmedia-codec pjmedia pjmedia-audiodev pjmedia-videodev
    pjnath pjlib-util libsrtp libresample libgsmcodec libspeex
    libilbccodec libg7221codec libyuv libwebrtc libbaseclasses pjlib
)
file(GLOB_RECURSE _PJSIP_ALL_LIBRARY_FILES CONFIGURE_DEPENDS "${_PJSIP_ROOT}/*.lib")
set(_PJSIP_LIBRARY_FILES "")
foreach(_candidate IN LISTS _PJSIP_ALL_LIBRARY_FILES)
    get_filename_component(_candidate_directory "${_candidate}" DIRECTORY)
    get_filename_component(_candidate_directory_name "${_candidate_directory}" NAME)
    if(_candidate_directory_name STREQUAL "lib")
        list(APPEND _PJSIP_LIBRARY_FILES "${_candidate}")
    endif()
endforeach()

function(_polphone_find_pjsip_library _base_name _pj_configuration _out_var)
    set(_matches "")
    foreach(_library_file IN LISTS _PJSIP_LIBRARY_FILES)
        get_filename_component(_library_name "${_library_file}" NAME)
        if(_library_name MATCHES "^${_base_name}-x86_64-x64-vc[^-]+-${_pj_configuration}\\.lib$")
            list(APPEND _matches "${_library_file}")
        endif()
    endforeach()
    list(LENGTH _matches _match_count)
    if(_match_count EQUAL 0)
        message(FATAL_ERROR "Biblioteca '${_base_name}' (${_pj_configuration}, x64) não encontrada. Rode scripts/setup-pjproject.ps1 -Config Both primeiro.")
    elseif(_match_count GREATER 1)
        list(JOIN _matches "\n  " _match_list)
        message(FATAL_ERROR "Mais de uma biblioteca corresponde a '${_base_name}' (${_pj_configuration}):\n  ${_match_list}")
    endif()
    list(GET _matches 0 _match)
    set(${_out_var} "${_match}" PARENT_SCOPE)
endfunction()

set(_PJSIP_IMPORTED_TARGETS "")
foreach(_base_name IN LISTS _PJSIP_EXPECTED_LIBRARIES)
    _polphone_find_pjsip_library("${_base_name}" "Debug-Dynamic" _debug_library)
    _polphone_find_pjsip_library("${_base_name}" "Release-Dynamic" _release_library)
    string(REPLACE "-" "_" _target_id "${_base_name}")
    set(_target_name "PJSIP::lib_${_target_id}")
    add_library("${_target_name}" STATIC IMPORTED GLOBAL)
    set_target_properties("${_target_name}" PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${_debug_library}"
        IMPORTED_LOCATION_RELEASE "${_release_library}"
    )
    list(APPEND _PJSIP_IMPORTED_TARGETS "${_target_name}")
endforeach()

add_library(PJSIP::pjsua2 INTERFACE IMPORTED GLOBAL)
set_target_properties(PJSIP::pjsua2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_PJSIP_ROOT}/pjlib/include;${_PJSIP_ROOT}/pjlib-util/include;${_PJSIP_ROOT}/pjnath/include;${_PJSIP_ROOT}/pjmedia/include;${_PJSIP_ROOT}/pjsip/include"
    INTERFACE_LINK_LIBRARIES "${_PJSIP_IMPORTED_TARGETS};ws2_32;mswsock;iphlpapi;winmm;dsound;dxguid;ole32;oleaut32;user32;gdi32;advapi32;netapi32;secur32"
)
