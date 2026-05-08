#!/bin/bash
set -e

if ! command -v unzip &> /dev/null; then
  sudo apt-get update -qq && sudo apt-get install -y unzip > /dev/null
fi

if ! command -v docker &> /dev/null; then
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker "$USER"
  exec sg docker "$0"
fi

curl -fsSL https://github.com/Meisdy/Job-App/archive/refs/heads/master.zip -o Job-App.zip
unzip -q Job-App.zip
mv Job-App-master Job-App
rm Job-App.zip
cd Job-App

cat > config/api_keys.json << 'EOF'
{
  "api_key": "YOUR_API_KEY_HERE"
}
EOF

mkdir -p data
chmod +x update.sh update_dev.sh

echo "Starting Job-App..."
docker compose pull
docker compose up -d

echo ""
echo "Done. Open http://localhost:8080 and complete onboarding."
echo "Onboarding lets you pick your AI provider and enter your API key — no file editing needed."
echo ""
echo "To update later: cd ~/Job-App && bash update.sh"
