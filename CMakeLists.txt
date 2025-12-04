cmake_minimum_required(VERSION 3.14)

project(whttp-server)

# 设置 C 标准为 C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # 可选：禁用 GNU 扩展，使用严格的 C11

# 设置 C++ 标准为 C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 可选：禁用 GNU 扩展，使用严格的 C++11

# linux系统下可以打开这个宏，那么底层就由select切换成epoll了
# add_definitions(-DUSE_EPOLL)

set(CXX_FLAGS_TMP
        -fPIC
        -Wall
        -O2
        -g
        )


if(APPLE)
    message("当前系统是 macOS")
    include_directories(
        /usr/local/openssl/include
    )
    link_directories(
            /usr/local/openssl/lib
        )
elseif(UNIX AND NOT APPLE)
    message("当前系统是 Linux 或其他UNIX系统")
    set(CXX_FLAGS_TMP ${CXX_FLAGS_TMP}
               -fsanitize=address
               -fno-omit-frame-pointer
               -fsanitize=leak
               -fsanitize-recover=address
               -fsanitize=undefined
               -lasan
        )
endif()

string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS_TMP}")
message(STATUS "${CMAKE_PROJECT_NAME} build CXX_FLAGS = ${CMAKE_CXX_FLAGS} ")

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
