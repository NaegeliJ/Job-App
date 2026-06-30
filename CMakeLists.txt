cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    set(APP_VERSION "unknown")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

if(WIN32)
    set(WIN_COMPAT_SRC src/win_compat.c)
endif()
add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp src/http.cpp src/html.cpp src/config.cpp src/ai.cpp src/fitcheck.cpp ${WIN_COMPAT_SRC})

target_include_directories(Job_App PRIVATE include/)

if(WIN32)
    if(NOT DEFINED CURL_WIN_DIR OR CURL_WIN_DIR STREQUAL "")
        message(FATAL_ERROR "Set -DCURL_WIN_DIR=<path> or run scripts/build-win.sh")
    endif()
    target_compile_definitions(Job_App PRIVATE CURL_STATICLIB)
    target_include_directories(Job_App PRIVATE "${CURL_WIN_DIR}/include")
    target_link_libraries(Job_App
        "${CURL_WIN_DIR}/lib/libcurl.a"
        "${CURL_WIN_DIR}/lib/libssl.a"
        "${CURL_WIN_DIR}/lib/libcrypto.a"
        "${CURL_WIN_DIR}/lib/libnghttp2.a"
        "${CURL_WIN_DIR}/lib/libnghttp3.a"
        "${CURL_WIN_DIR}/lib/libngtcp2.a"
        "${CURL_WIN_DIR}/lib/libngtcp2_crypto_libressl.a"
        "${CURL_WIN_DIR}/lib/libssh2.a"
        "${CURL_WIN_DIR}/lib/libpsl.a"
        "${CURL_WIN_DIR}/lib/libbrotlidec.a"
        "${CURL_WIN_DIR}/lib/libbrotlicommon.a"
        "${CURL_WIN_DIR}/lib/libzstd.a"
        "${CURL_WIN_DIR}/lib/libz.a"
        ws2_32 wldap32 crypt32 secur32 bcrypt iphlpapi
        /usr/x86_64-w64-mingw32/lib/libwinpthread.a
    )
    target_link_options(Job_App PRIVATE -static)
else()
    target_link_libraries(Job_App curl)
endif()
