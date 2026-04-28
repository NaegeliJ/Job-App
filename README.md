# Job Radar

Personal job-market radar: scrape listings from jobs.ch, score them against your profile with an LLM, and track applications in one UI.

## What it does

1. **Scrape** — Fetches job listings from jobs.ch based on your search queries.
2. **Fit-check** — Sends every job + your profile to an LLM. Stores a `fit_label` (Strong, Decent, Experimental, Weak, No Go), a weighted score, and structured reasoning you can read in seconds.
3. **Track** — Keep notes, set a status (New, Applied, etc.), and rate jobs. Sort, filter, and search everything in the browser.

## Who it's for

- Job seekers who want to stop manually reading hundreds of postings.
- Anyone with an LLM API endpoint who wants to let AI do the first-pass filtering.
- Developers who want a hackable, self-hosted C++ + vanilla-JS stack with no bundler and no SaaS.

## Quick start

Quickstart focuses on Docker only for now. On a linux machine with Docker installed, execute: 

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

Open **http://localhost:8080** and complete onboarding. The first screen lets you pick your AI provider, enter the endpoint and model, and paste your API key — no file editing required. The CV step accepts a PDF drop (extracted in-browser) or plain paste. Scraping and fit-checking happen inside the app after that.

**WSL users:** Docker does not auto-start on WSL boot. Start manually via `docker start job-app`, or add to autostart `~/.bashrc` if you want it automatic:

```bash
sudo service docker start 2>/dev/null
cd ~/Job-App && docker compose up -d 2>/dev/null
```

## AI provider setup

Provider, endpoint, model, and API key are all configured inside the app — open **Settings** (gear icon) or set them during onboarding. You do not need to edit config files manually.

**Supported providers (tested):**

| Provider | Notes |
|----------|-------|
| Ollama (local) | No API key needed. Default endpoint `http://localhost:11434/api/chat`. Make sure Ollama listens on `0.0.0.0` inside Docker: `OLLAMA_HOST=0.0.0.0 ollama serve`. |
| Ollama Cloud | Requires API key. Endpoint `https://ollama.com/v1/chat/completions`. |
| OpenRouter | Requires API key. Endpoint `https://openrouter.ai/api/v1/chat/completions`. |
| DeepInfra | Requires API key. Endpoint `https://api.deepinfra.com/v1/openai/chat/completions`. |
| Mistral | Requires API key. Endpoint `https://api.mistral.ai/v1/chat/completions`. |
| Custom | Any endpoint accepting `Authorization: Bearer <key>` + OpenAI-compatible chat body. |

**Not supported:** Anthropic native API (`x-api-key` header, different request format).

### How the backend builds requests

The backend sends provider-aware requests — not all fields are sent to all providers:

| Field | Ollama (local) | Ollama Cloud / OpenRouter / Mistral | DeepInfra / Custom |
|-------|---------------|-------------------------------------|--------------------|
| `format: "json"` | ✅ | — | — |
| `response_format: {type: "json_object"}` | — | ✅ | — |
| `top_k` | ✅ | — | — |
| `stream: false` | ✅ | ✅ | ✅ |

The response is parsed as either Ollama native NDJSON or OpenAI-compatible SSE automatically.

## How it works

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Scrape    │────▶│   Details   │────▶│  Fit-check  │────▶│   Track     │
│  jobs.ch    │     │ fetch text  │     │   LLM call  │     │   in UI     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

1. Click **Scrape** in the UI. The backend hits jobs.ch for each query in `config_v2.json`, saves new listings to SQLite, then fetches the full posting text.
2. Click **Fit-Check**. The backend sends every job with `template_text` but no `fit_label` to your LLM endpoint in batches of `limit`. Results are written back to the DB.
3. Sort by fit score, filter by label, read AI reasoning, and decide whether to apply.

You can also import a job from pasted text (`Import Text`), recheck a single job, or clear all fit data and re-run from the admin console.

## Configuration

Most settings are editable live in the app (Settings gear). Config files in `config/` on the host are the source of truth and survive container rebuilds.

### `api_keys.json`

Single key used for LLM calls. Written by the app when you save settings — you rarely need to touch this directly.

```json
{ "api_key": "YOUR_API_KEY_HERE" }
```

For Ollama local this can be `""`.

### `config_v2.json`

Controls scraping and LLM parameters. Editable in Settings without restart.

| Field | Meaning |
|-------|---------|
| `scrape.queries` | Array of search strings sent to jobs.ch. |
| `scrape.rows` | Max listings to fetch per query. |
| `fitcheck.provider` | Provider key (`ollama_local`, `ollama_cloud`, `openrouter`, `deepinfra`, `mistral`, `custom`). |
| `fitcheck.endpoint` | LLM HTTP endpoint. |
| `fitcheck.model` | Model identifier (provider-specific). |
| `fitcheck.limit` | Max jobs to fit-check in one batch call. |
| `fitcheck.max_tokens` | Max response tokens per LLM call. |
| `fitcheck.temperature` | LLM temperature. |
| `fitcheck.top_p` | Nucleus sampling. |
| `fitcheck.top_k` | Top-k sampling (Ollama / some providers). |

### `system_prompt.txt`

The prompt template sent to the LLM. Must contain exactly two placeholders:

- `{{profile}}` — replaced with your profile text.
- `{{jobText}}` — replaced with the job posting text.

The default prompt asks for structured JSON output including `fit_score`, `fit_label`, `fit_summary`, `dimension_scores`, `strengths`, `gaps`, `fit_reasoning`, `hiring_chances`, and `verdict`.

**Important:** If `system_prompt.txt` is missing or lacks both placeholders, the server refuses to start.

### `user_profile.md`

Created after onboarding (or edited in **Profile**). Plain Markdown describing your skills, constraints, experience, and No-Gos. This is what gets substituted into `{{profile}}` during fit-check.

## Project layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | HTTP server, API endpoints, LLM streaming helpers |
| `src/db.cpp` | SQLite operations, migrations |
| `frontend/index.html` | Main SPA |
| `frontend/onboarding.html` | Onboarding wizard (generates `user_profile.md`) |
| `frontend/css/` / `frontend/js/` | Vanilla ES6 modules, no bundler |
| `config/config_v2.json` | Scrape + LLM settings |
| `config/system_prompt.txt` | Fit-check prompt template |
| `config/api_keys.json` | API key (gitignored) |
| `data/` | SQLite database (bind-mounted) |

## Admin console

Press `Ctrl + \` in the browser to open the dev admin console. Commands work on partial job IDs (last 8 characters). `help` shows available commands. Key operations:

- Delete a job by partial ID.
- Clear fit data for one or all jobs.
- Recheck one or all jobs via LLM.

## Updating

```bash
cd ~/Job-App
bash update.sh
```

Downloads the latest version and rebuilds the container. Database and config survive.

## Logs

```bash
cd ~/Job-App
docker compose logs -f
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Bind for 0.0.0.0:8080 failed: port already allocated` | Stop the old container: `docker compose down` in the other project, or change the port mapping in `docker-compose.yml`. |
| `Connection refused` to LLM endpoint | Check the endpoint in Settings. For local Ollama inside Docker, Ollama must listen on `0.0.0.0`: `OLLAMA_HOST=0.0.0.0 ollama serve`. Check firewall. |
| LLM returns empty response or times out | The backend retries once automatically. If it still fails, check that the model name is correct and the endpoint returns JSON/SSE correctly. Backend timeout is fixed at 600s. |
| Scrape returns 0 jobs | Verify `scrape.queries` in `config_v2.json`. Check logs for HTTP errors from jobs.ch. |
| Onboarding or profile not saving | Profile is written to `config/user_profile.md`. Check that the `config/` volume mount is working and the container can write there. |
| PDF drop shows no text | PDF.js extracts text from selectable PDFs only — scanned/image PDFs yield nothing. Paste CV text manually instead. |
| Fit-check is slow | Increase `fitcheck.limit` if your endpoint handles concurrency well. Decrease if you hit rate limits. Check `max_tokens` — too high wastes time on long reasoning. |

## Uninstall

Stop and remove:

```bash
cd ~/Job-App
docker compose down
```

Remove everything (data + config + code):

```bash
docker compose down
cd ~
rm -rf ~/Job-App
```

Also remove the image if you want to free disk space:

```bash
docker compose down --rmi all
```

## Local build (no Docker)

```bash
# Dependencies
sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev

# Build
rm -rf cmake-build-debug && mkdir cmake-build-debug
cd cmake-build-debug && cmake .. && cd ..
cmake --build cmake-build-debug

# Run
./cmake-build-debug/Job_App
```

The server starts on port 8080. Make sure `config/` and `data/` exist next to the binary.
