include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

foreach(test_name test_tokenizer test_parser test_storage test_database test_session test_buffer_pool test_validation test_page test_page_manager test_bplus_tree test_index_manager test_index_integration )
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE kerneldb_core Catch2::Catch2WithMain)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()