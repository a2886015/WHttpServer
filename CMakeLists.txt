cmake_minimum_required(VERSION 3.14)

project(whttp-server)
add_compile_options(-std=c++11)

# linux系统下可以打开这个宏，那么底层就由select切换成epoll了
# add_definitions(-DUSE_EPOLL)

FILE (GLOB_RECURSE ALL_SOURCES
        "*.cpp" "*.c"
)

FILE (GLOB_RECURSE ALL_INCLUDES
    "*.h" "*.hpp"
)

include_directories(
    ${CMAKE_SOURCE_DIR}/whttp-server-core
)

add_executable(${PROJECT_NAME}
               ${ALL_SOURCES}
               ${ALL_INCLUDES}
)

target_link_libraries(${PROJECT_NAME}
                      pthread
                      ssl
                      crypto
)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
