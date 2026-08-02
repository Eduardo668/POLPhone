cmake_minimum_required(VERSION 3.21)
project(POLPhone VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "POLPhone tem como alvo exclusivo Windows x64. Configure com Visual Studio 2022 em uma máquina Windows.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "POLPhone requer MSVC/Visual Studio 2022 com o toolset v143.")
endif()
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio 17 2022")
    message(FATAL_ERROR "Use o gerador 'Visual Studio 17 2022' (scripts/build.ps1 faz isso automaticamente).")
endif()
if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "POLPhone deve ser configurado com a plataforma x64 (-A x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Configurações suportadas" FORCE)
option(POLPHONE_BUILD_TESTS "Compilar testes unitários (disponível a partir da T04)" OFF)

include(cmake/PJSIP.cmake)
include(cmake/Warnings.cmake)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

add_library(polphone_util STATIC
    src/util/Strings.cpp
    src/util/Time.cpp
)
target_include_directories(polphone_util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_util PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_link_libraries(polphone_util PRIVATE polphone_warnings)
target_compile_options(polphone_util PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_logging STATIC
    src/logging/Logger.cpp
    src/logging/Redactor.cpp
)
target_include_directories(polphone_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(polphone_logging PUBLIC polphone_util PRIVATE polphone_warnings)
target_compile_options(polphone_logging PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_config STATIC
    src/config/AppConfig.cpp
    src/config/ConfigLoader.cpp
    src/config/ConfigValidator.cpp
)
target_include_directories(polphone_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(polphone_config
    PRIVATE nlohmann_json polphone_warnings
)
target_compile_options(polphone_config PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

# Isola pjsua2.hpp no adaptador; Logger, Redactor e sinks permanecem puros.
add_library(polphone_pjsip_logging STATIC
    src/logging/PjLogWriter.cpp
)
target_include_directories(polphone_pjsip_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_pjsip_logging PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone_pjsip_logging
    PUBLIC polphone_logging
    PRIVATE PJSIP::pjsua2 polphone_warnings
)
target_compile_options(polphone_pjsip_logging PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_state STATIC
    src/app/AppState.cpp
)
target_include_directories(polphone_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(polphone_state PRIVATE polphone_warnings)
target_compile_options(polphone_state PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_sip STATIC
    src/sip/CallRegistry.cpp
    src/sip/PjErrors.cpp
    src/sip/SipAccount.cpp
    src/sip/SipCall.cpp
    src/sip/SipEndpoint.cpp
)
target_include_directories(polphone_sip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_sip PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone_sip
    PUBLIC PJSIP::pjsua2 polphone_config polphone_logging polphone_pjsip_logging polphone_state
    PRIVATE polphone_warnings
)
target_compile_options(polphone_sip PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_audio STATIC
    src/audio/AudioDeviceService.cpp
    src/audio/ToneGenerator.cpp
)
target_include_directories(polphone_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_audio PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone_audio
    PUBLIC PJSIP::pjsua2 polphone_config polphone_logging polphone_sip
    PRIVATE polphone_warnings
)
target_compile_options(polphone_audio PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_dtmf STATIC
    src/dtmf/DtmfPlan.cpp
    src/dtmf/DtmfRequestGate.cpp
    src/dtmf/DtmfSender.cpp
)
target_include_directories(polphone_dtmf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_dtmf PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone_dtmf
    PUBLIC PJSIP::pjsua2 polphone_audio polphone_config polphone_logging polphone_sip polphone_state polphone_util
    PRIVATE polphone_warnings
)
target_compile_options(polphone_dtmf PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_library(polphone_app STATIC
    src/app/Application.cpp
    src/app/CommandParser.cpp
    src/app/ConsoleUi.cpp
)
target_include_directories(polphone_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(polphone_app PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone_app
    PUBLIC polphone_audio polphone_config polphone_dtmf polphone_logging polphone_sip polphone_state
    PRIVATE polphone_warnings
)
target_compile_options(polphone_app PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)

add_executable(polphone src/main.cpp)
target_compile_definitions(polphone PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    UNICODE
    _UNICODE
    PJ_WIN32=1
    PJ_M_X86_64=1
)
target_link_libraries(polphone PRIVATE
    PJSIP::pjsua2
    nlohmann_json
    polphone_util
    polphone_config
    polphone_logging
    polphone_dtmf
    polphone_pjsip_logging
    polphone_sip
    polphone_audio
    polphone_app
    polphone_warnings
)
target_compile_options(polphone PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)
target_link_options(polphone PRIVATE $<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>)
set_target_properties(polphone PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT polphone)

if(POLPHONE_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(FATAL_ERROR "O alvo de testes pertence à T04 e ainda não existe.")
    endif()
endif()
