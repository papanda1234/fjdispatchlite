cmake_minimum_required(VERSION 3.10)

project(test_dispatch)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR})

# test_fixvector 実行ファイルの設定
add_executable(test_fixvector fjtypes.cpp test/test_fixvector.cpp)
target_link_libraries(test_fixvector pthread)
set_target_properties(test_fixvector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_sharednotify 実行ファイルの設定
add_executable(test_sharednotify fjsharedmem.cpp fjtypes.cpp test/test_sharednotify.cpp)
target_link_libraries(test_sharednotify pthread)
target_link_libraries(test_sharednotify rt)
set_target_properties(test_sharednotify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_sharedupdate 実行ファイルの設定
add_executable(test_sharedupdate fjsharedmem.cpp fjtypes.cpp test/test_sharedupdate.cpp)
target_link_libraries(test_sharedupdate pthread)
target_link_libraries(test_sharedupdate rt)
set_target_properties(test_sharedupdate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_sharedpayload_server/client (payload notify + updateWithData)
add_executable(test_sharedpayload_server fjsharedmem.cpp fjtypes.cpp test/test_sharedpayload_server.cpp)
target_link_libraries(test_sharedpayload_server pthread)
target_link_libraries(test_sharedpayload_server rt)
set_target_properties(test_sharedpayload_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

add_executable(test_sharedpayload_client fjsharedmem.cpp fjtypes.cpp test/test_sharedpayload_client.cpp)
target_link_libraries(test_sharedpayload_client pthread)
target_link_libraries(test_sharedpayload_client rt)
set_target_properties(test_sharedpayload_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)


# test_dispatch 実行ファイルの設定
add_executable(test_dispatch fjtypes.cpp test/test_dispatch.cpp)
target_link_libraries(test_dispatch pthread)
set_target_properties(test_dispatch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_timer 実行ファイルの設定
add_executable(test_timer fjtypes.cpp test/test_timer.cpp)
target_link_libraries(test_timer pthread)
set_target_properties(test_timer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

# test_autosnake server/client
add_executable(test_autosnake_server fjsharedmem.cpp fjtypes.cpp test/test_autosnake_server.cpp)
target_link_libraries(test_autosnake_server pthread)
target_link_libraries(test_autosnake_server rt)
set_target_properties(test_autosnake_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)

add_executable(test_autosnake_client fjsharedmem.cpp fjtypes.cpp test/test_autosnake_client.cpp)
target_link_libraries(test_autosnake_client pthread)
target_link_libraries(test_autosnake_client rt)
set_target_properties(test_autosnake_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build
)
