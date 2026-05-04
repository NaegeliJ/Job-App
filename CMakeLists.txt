cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    set(APP_VERSION "unknown")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp)

target_include_directories(Job_App PRIVATE include/)
target_link_libraries(Job_App curl)
