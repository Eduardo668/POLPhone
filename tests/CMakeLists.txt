add_executable(polphone_tests
    test_main.cpp
    test_strings.cpp
)

target_link_libraries(polphone_tests PRIVATE doctest polphone_util polphone_warnings)
target_compile_definitions(polphone_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(polphone_tests PRIVATE
    $<$<CONFIG:Debug>:/Zi;/Od;/RTC1>
    $<$<CONFIG:Release>:/O2;/Zi>
)
set_target_properties(polphone_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

add_test(NAME polphone_tests COMMAND polphone_tests)
