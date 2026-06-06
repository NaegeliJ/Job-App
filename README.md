# Jobs Master

Scrape job listings from jobs.ch or import a job manually, score them against your profile with an LLM, and track applications — all in one self-hosted UI.

## Quick start

Two ways to run: a standalone Windows exe (no Docker) or the Docker image on Linux.

### Windows (standalone exe)

Download `Job_App-<version>-win64.zip` from the [Releases page](https://github.com/Meisdy/Job-App/releases/latest), unzip anywhere, and run `Job_App.exe`. No install, no DLLs, no Docker. The exe creates its `data/` folder on first run and opens your browser automatically.

The zip contains the exe alongside `config/` and `frontend/` — keep them together in the same folder.

### Linux (Docker)

```bash
curl -fsSL https://raw.githubusercontent.com/Meisdy/Job-App/master/setup.sh | bash
```

### First run

Open **http://localhost:8080** and complete onboarding. Pick your AI provider, enter the endpoint and model, paste your API key (or leave blank for local Ollama). The CV step accepts a PDF drop or plain text paste.

**Supported AI providers:** Ollama (local/cloud), OpenRouter, DeepInfra, Mistral, or any custom OpenAI-compatible endpoint. Anthropic native API is not supported.

**WSL users:** Docker does not auto-start on WSL boot. Start manually with `docker start job-app`, or add to `~/.bashrc`:

```bash
sudo service docker start > /dev/null 2>&1
```

## How it works

```
 Scrape ──▶ Fit-check ──▶ Track 
```

1. **Scrape** — click Scrape in the UI. Backend hits jobs.ch for each query in your config (view via Settings), saves listings to SQLite, then fetches full posting text.
2. **Fit-check** — click Fit-Check. Every job with posting text but no score is sent to your LLM. Results: `fit_label` (Strong / Decent / Experimental / Weak / No Go), a weighted score, and structured reasoning. A progress bar tracks the batch; a fatal error (bad key, no credits, rate limit) stops the batch immediately and flags the button.
3. **Track** — sort by score, filter by label, read reasoning, set status, write notes, rate jobs.

**First-time setup tip:** Start with a small scrape (1–2 queries, low `rows`). Fit-check those jobs, read the reasoning, and adjust your profile or LLM settings until the scores feel right. Once satisfied, scrape the full set and run a full batch fit-check.

You can also paste a job posting directly via **Import Text** — the app extracts fields and fit-checks it automatically. To recheck a single job, use the recheck button on the detail panel.

All AI provider settings are configured in **Settings** (gear icon) — no file editing required. See [Configuration](#configuration) for advanced options, or the [Manual](MANUAL.md) for full feature reference.

## Configuration

Config files live in `config/` — on the host (Docker) or next to `Job_App.exe` (Windows). They survive container rebuilds and exe updates. Most fields are editable live in Settings without restart.

### `config_v2.json`

| Field | Meaning |
|-------|---------|
| `scrape.queries` | Search strings sent to jobs.ch |
| `scrape.rows` | Max listings per query |
| `fitcheck.provider` | Provider key (`ollama_local`, `ollama_cloud`, `openrouter`, `deepinfra`, `mistral`, `custom`) |
| `fitcheck.endpoint` | LLM HTTP endpoint |
| `fitcheck.model` | Model identifier (provider-specific) |
| `fitcheck.limit` | Max jobs per batch fit-check |
| `fitcheck.max_tokens` | Max response tokens per LLM call |
| `fitcheck.temperature` | LLM temperature |
| `fitcheck.top_p` | Nucleus sampling |
| `fitcheck.top_k` | Top-k sampling (Ollama only) |

### `system_prompt.txt`

Prompt template sent to the LLM. Must contain `{{profile}}` and `{{jobText}}` — both are required or the server refuses to start.

### `user_profile.md`

Generated during onboarding, editable in **Profile**. Plain Markdown describing your skills, constraints, and preferences. Substituted into `{{profile}}` during fit-check.

### `api_keys.json`

Single field `{ "api_key": "..." }`. Written by the app when you save settings. Leave empty for local Ollama.

## Admin console

Press `Ctrl+\` in the browser. Commands accept partial job IDs (last 8 chars). Type `help` for the full list. Key operations:

- Delete a job
- Clear fit data for one or all jobs
- Recheck one or all jobs via LLM

## Updating

**Docker:**

```bash
cd ~/Job-App
bash update.sh
```

Pulls the latest pre-built image from GitHub Container Registry (~30 seconds). Database and config survive. The UI shows a notice when a newer version is available.

**Windows exe:** download the newest `Job_App-<version>-win64.zip` from [Releases](https://github.com/Meisdy/Job-App/releases/latest) and replace `Job_App.exe`. Keep your existing `data/` and `config/api_keys.json` — your database and key survive.

## Logs

```bash
cd ~/Job-App
docker compose logs -f
```

## Disclaimer

This tool scrapes jobs.ch. Use it responsibly and at your own risk. The authors are not responsible for any consequences arising from its use.

## Uninstall

**Docker:**

```bash
cd ~/Job-App
docker compose down          # stop and remove container

# To remove everything (data + config + code):
docker compose down --rmi all
cd ~
rm -rf ~/Job-App
```

**Windows exe:** delete the unzipped folder. Nothing is installed system-wide.
