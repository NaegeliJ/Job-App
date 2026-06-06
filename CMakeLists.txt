cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)

if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    set(APP_VERSION "unknown")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp)

target_include_directories(Job_App PRIVATE include/)

if(WIN32)
    if(NOT DEFINED CURL_WIN_DIR OR CURL_WIN_DIR STREQUAL "")
        message(FATAL_ERROR "Set -DCURL_WIN_DIR=<path> or run scripts/build-win.sh")
    endif()
    target_include_directories(Job_App PRIVATE "${CURL_WIN_DIR}/include")
    target_link_libraries(Job_App
        "${CURL_WIN_DIR}/lib/libcurl.a"
        ws2_32 wldap32 crypt32
    )
    target_link_options(Job_App PRIVATE -static-libgcc -static-libstdc++)
else()
    target_link_libraries(Job_App curl)
endif()
