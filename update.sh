#!/bin/bash
set -e
BRANCH=${1:-master}
cd "$(dirname "$(realpath "$0")")"

echo "Downloading $BRANCH..."
curl -fsSL "https://github.com/Meisdy/Job-App/archive/refs/heads/$BRANCH.zip" -o _update.zip
unzip -q -o _update.zip
DIR="Job-App-$BRANCH"

if [ "$BRANCH" = "master" ]; then
  cp -r "$DIR/frontend" "$DIR/docker-compose.yml" "$DIR/update.sh" "$DIR/setup.sh" .
  rm -rf "$DIR" _update.zip
  chmod +x update.sh setup.sh
  echo "Pulling latest image..."
  docker compose pull
  docker compose up -d
else
  cp -r "$DIR/src" "$DIR/include" "$DIR/frontend" "$DIR/Dockerfile" \
        "$DIR/docker-compose.yml" "$DIR/CMakeLists.txt" \
        "$DIR/update.sh" "$DIR/setup.sh" .
  rm -rf "$DIR" _update.zip
  chmod +x update.sh setup.sh
  echo "Rebuilding container (takes ~2 min)..."
  docker compose up --build -d
fi

echo ""
echo "Done. http://localhost:8080"
