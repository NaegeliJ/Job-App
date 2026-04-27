# TODO

## Additional deployment methods
- Native Linux install script (no Docker) — single `install.sh` that handles deps + builds
- Windows native build (MSVC or MSYS2)
- Docker Hub image — `docker pull meisdy/job-app` instead of build-from-source
- GitHub Actions: auto-build + push image on tag

## Exe / binary distribution (no Docker)
- Static-link all deps (libcurl etc.) for single-file binary — no install required
- `start.bat` (Windows) + `start.sh` (Linux): launch server + auto-open browser, logs to terminal
- GitHub Actions: cross-compile and attach Windows exe + Linux bin to GitHub Releases on tag
- Ship as ZIP: binary + `frontend/` + `config/` template + launcher script
