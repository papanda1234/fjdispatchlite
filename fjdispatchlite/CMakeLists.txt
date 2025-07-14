cmake_minimum_required(VERSION 3.10)

project(test_dispatch)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR})

# test_fixvector 実行ファイルの設定
add_executable(test_fixvector test/test_fixvector.cpp)
target_link_libraries(test_fixvector pthread)
set_target_properties(test_fixvector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_sharednotify 実行ファイルの設定
add_executable(test_sharednotify fjsharedmem.cpp test/test_sharednotify.cpp)
target_link_libraries(test_sharednotify pthread)
target_link_libraries(test_sharednotify rt)
set_target_properties(test_sharednotify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_sharedupdate 実行ファイルの設定
add_executable(test_sharedupdate fjsharedmem.cpp test/test_sharedupdate.cpp)
target_link_libraries(test_sharedupdate pthread)
target_link_libraries(test_sharedupdate rt)
set_target_properties(test_sharedupdate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)


# test_dispatch 実行ファイルの設定
add_executable(test_dispatch test/test_dispatch.cpp)
target_link_libraries(test_dispatch pthread)
set_target_properties(test_dispatch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_timer 実行ファイルの設定
add_executable(test_timer test/test_timer.cpp)
target_link_libraries(test_timer pthread)
set_target_properties(test_timer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_reader 実行ファイルの設定
add_executable(test_reader fjmediaqueue.cpp test/test_reader.cpp)
target_link_libraries(test_reader pthread)
target_link_libraries(test_reader rt)
set_target_properties(test_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_writer 実行ファイルの設定
add_executable(test_writer fjmediaqueue.cpp test/test_writer.cpp)
target_link_libraries(test_writer pthread)
target_link_libraries(test_writer rt)
set_target_properties(test_writer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

