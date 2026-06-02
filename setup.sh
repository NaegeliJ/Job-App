#!/bin/bash
set -e

if ! command -v unzip &> /dev/null; then
  sudo apt-get update -qq && sudo apt-get install -y unzip > /dev/null
fi

if ! command -v docker &> /dev/null; then
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker "$USER"
fi

if ! docker info &> /dev/null; then
  echo "Docker installed. Log out and back in, then re-run setup.sh"
  exit 0
fi

RAW="https://raw.githubusercontent.com/Meisdy/Job-App/master"
mkdir -p ~/Job-App/config ~/Job-App/data
cd ~/Job-App

curl -fsSL "$RAW/docker-compose.yml" -o docker-compose.yml
curl -fsSL "$RAW/update.sh"          -o update.sh
curl -fsSL "$RAW/config/config_v2.json"           -o config/config_v2.json
curl -fsSL "$RAW/config/system_prompt.txt"        -o config/system_prompt.txt
curl -fsSL "$RAW/config/user_profile_template.md" -o config/user_profile_template.md
chmod +x update.sh

cat > config/api_keys.json << 'EOF'
{
  "api_key": "YOUR_API_KEY_HERE"
}
EOF

echo "Starting Job-App..."
docker compose pull 2>/dev/null && docker compose up -d || {
  echo "Image not available yet, building from source (~2 min)..."
  docker compose up --build -d
}

echo ""
echo "Done. Open http://localhost:8080 and complete onboarding."
echo "Onboarding lets you pick your AI provider and enter your API key — no file editing needed."
echo ""
echo "To update later: cd ~/Job-App && bash update.sh"
