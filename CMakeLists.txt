cmake_minimum_required(VERSION 3.14)

project(whttp-server)
add_compile_options(-std=c++11)

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
