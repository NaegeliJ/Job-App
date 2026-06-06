#!/bin/bash
set -e
cd "$(dirname "$0")/.."

# Check for mingw compiler
if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
    echo "Installing mingw-w64..."
    sudo apt-get install -y mingw-w64
fi

CURL_DIR="deps/curl-win"

if [ ! -f "${CURL_DIR}/lib/libcurl.a" ]; then
    echo "Fetching static curl for mingw64..."
    # Get latest version tag from curl.se
    CURL_VER=$(curl -s "https://curl.se/windows/" \
        | grep -oP 'dl-\K[0-9]+\.[0-9]+\.[0-9]+_[0-9]+(?=/)' \
        | sort -V | tail -1)
    if [ -z "$CURL_VER" ]; then
        echo "ERROR: could not detect curl version from curl.se/windows"
        exit 1
    fi
    CURL_ZIP="curl-${CURL_VER}-win64-mingw.zip"
    echo "Downloading ${CURL_ZIP}..."
    mkdir -p deps
    curl -L -o "deps/${CURL_ZIP}" "https://curl.se/windows/dl-${CURL_VER}/${CURL_ZIP}"
    cd deps
    unzip -q "${CURL_ZIP}"
    mv "curl-${CURL_VER}-win64-mingw" curl-win
    rm "${CURL_ZIP}"
    cd ..
    if [ ! -f "${CURL_DIR}/lib/libcurl.a" ]; then
        echo "ERROR: libcurl.a not found in downloaded package — check ${CURL_DIR}/lib/"
        ls "${CURL_DIR}/lib/" 2>/dev/null || true
        exit 1
    fi
    echo "curl ${CURL_VER} ready at ${CURL_DIR}"
fi

# APP_VERSION: from arg, else env, else "dev"
APP_VERSION="${1:-${APP_VERSION:-dev}}"

cmake -B build-win \
    -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake \
    -DCURL_WIN_DIR="$(pwd)/${CURL_DIR}" \
    -DAPP_VERSION="${APP_VERSION}"
cmake --build build-win --parallel

echo ""
echo "Done: build-win/Job_App.exe"
echo "Copy to Windows alongside frontend/ and config/"
