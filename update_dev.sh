#!/bin/bash
set -e

cd ~/Job-App

echo "Downloading dev branch..."
curl -fsSL https://github.com/Meisdy/Job-App/archive/refs/heads/dev.zip -o _update.zip
unzip -q -o _update.zip
sed -i 's/\r//' Job-App-dev/update.sh Job-App-dev/update_dev.sh Job-App-dev/setup.sh
cp -r Job-App-dev/src Job-App-dev/include Job-App-dev/frontend Job-App-dev/Dockerfile \
      Job-App-dev/docker-compose.yml Job-App-dev/CMakeLists.txt \
      Job-App-dev/update.sh Job-App-dev/update_dev.sh .
rm -rf Job-App-dev _update.zip

echo "Rebuilding container (takes ~2 min)..."
docker compose up --build -d

echo ""
echo "Done. Open http://localhost:8080"
