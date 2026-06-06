# TODO

## UX improvements

## Additional deployment methods
- Native Linux install script (no Docker) — single `install.sh` that handles deps + builds
- Docker Hub image — `docker pull meisdy/job-app` instead of build-from-source
- [x] Windows native build — static mingw-w64 cross-compile
- [x] GitHub Actions: auto-build + push image on tag

## Exe / binary distribution (no Docker)
- Linux static bin: cross-compile + attach to Releases on tag
- [x] Static-link all deps (libcurl etc.) for single-file Windows exe — no install required
- [x] Auto-open browser on launch (ShellExecute / xdg-open)
- [x] GitHub Actions: cross-compile and attach Windows exe to GitHub Releases on tag
- [x] Ship as ZIP: exe + `frontend/` + `config/` templates
