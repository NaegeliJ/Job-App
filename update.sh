#!/bin/bash
set -e

cd ~/Job-App

echo "Downloading latest version..."
curl -fsSL https://github.com/Meisdy/Job-App/archive/refs/heads/master.zip -o _update.zip
unzip -q -o _update.zip
sed -i 's/\r//' Job-App-master/update.sh Job-App-master/setup.sh
cp -r Job-App-master/src Job-App-master/include Job-App-master/frontend Job-App-master/Dockerfile \
      Job-App-master/docker-compose.yml Job-App-master/CMakeLists.txt .
rm -rf Job-App-master _update.zip

echo "Rebuilding container (takes ~2 min)..."
docker compose up --build -d

echo ""
echo "Done. Open http://localhost:8080"
