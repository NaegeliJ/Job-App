# Job-App Agent Guidelines

Build, test, and code-style guidance for agentic coding in the Job-App repository.

## ⚠️ Stale Docs

`frontend/README.md` and `frontend/js/README.md` are stale. Trust this file and live source instead.

## 📁 Project Structure

| | |
|---|---|
| `config/config_v2.json` | Active scrape & fitcheck config |
| `config/system_prompt.txt` | LLM prompt template (`{{profile}}`, `{{jobText}}` placeholders) |
| `config/api_keys.json` | API keys (gitignored) |
| `setup.sh` | One-liner Docker setup (creates `api_keys.json` template) |
| `Dockerfile` / `docker-compose.yml` | Multi-stage Debian build, mounts `./data` and `./config` |
| `src/main.cpp` | Server, all API endpoints, config, HTTP helpers |
| `src/db.cpp` | Database operations (SQLite) |
| `frontend/index.html` | Main SPA |
| `frontend/onboarding.html` | Onboarding wizard (separate page) |
| `frontend/css/` / `frontend/js/` | ES6 modules, no bundler |

### Frontend Architecture

- **CSS Variables**: All colors in `css/variables.css`. Text colors: `--text`, `--text2`, `--text3` — **no `--text1`**
- **JS Modules**: ES6 modules, no bundler. `state.js` is the single source of truth.
- **api.js exports**: `GET_URL`, `UPDATE_URL`, `SCRAPE_URL`, `DETAILS_URL`, `CONFIG_GET_URL`, `CONFIG_POST_URL`, `AI_CONFIG_GET_URL`, `AI_CONFIG_POST_URL`, `PROFILE_GET_URL`, `PROFILE_SAVE_URL`, `FITCHECK_URL`, `IMPORT_TEXT_URL`
- **XSS**: All user/LLM data inserted into `innerHTML` must go through `escapeHtml()` from `formatting.js`
- **Header Layout**: Logo, status dot, `.search-group` (absolutely centered), profile, settings (left → right). Profile has `margin-left: auto`. Header gap is 8px. No filter buttons in header.
- **Filter Dropdown**: Lives in `.sb-header` between "Positions" label and `⇅ SCORE` sort button. `#filter-dropdown-btn` triggers `#filter-dropdown-menu` (`.open` class toggle). Open/close wired in `main.js` `bindEvents` (click trigger + document click-outside + Escape key).
- **No inline onclick handlers:** All event wiring is in `main.js` `bindEvents()`. Elements have `id` attributes; handlers attach via `addEventListener`.
- **Onboarding CV Drop (Q1):** Drop zone + browse button above the textarea. PDF.js 3.11.174 (CDN) extracts text client-side and fills the textarea. No new backend endpoint — `answers[0]` is still plain text when submitted. `#cv-drop-zone` shown only for `index === 0` via `showQuestion()`.

### Admin Console

Dev console toggles with `Ctrl+\` in browser. Calls admin endpoints under `/api/admin/`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/admin/jobs/:id` | DELETE | Delete job |
| `/api/admin/fitcheck/clear/:id` | POST | Clear fit data for one job |
| `/api/admin/fitcheck/clear` | POST | Clear fit data for ALL jobs |
| `/api/admin/fitcheck/recheck/:id` | POST | Clear + recheck one job via LLM |
| `/api/admin/fitcheck/recheck` | POST | Clear all fit data (re-queue for batch) |

Console resolves partial job IDs (last 8 chars) via `state.allJobs` suffix matching.

## 🚀 Build System

Build directory is `cmake-build-debug`.

```bash
# Incremental build
cmake --build cmake-build-debug

# Clean build
rm -rf cmake-build-debug && mkdir cmake-build-debug
cd cmake-build-debug && cmake .. && cd ..
cmake --build cmake-build-debug

# Run
./cmake-build-debug/Job_App
# Access at http://localhost:8080
```

### Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update && sudo apt install -y cmake g++ make libsqlite3-dev libcurl4-openssl-dev
```

## 🌐 API Endpoints

### V2 Pipeline (active)

| Endpoint | Method | Purpose |
|---|---|---|
| `GET /api/jobs` | — | Fetch all jobs (JobRecord array) |
| `POST /api/jobs/update` | — | Update user_status / rating / notes |
| `DELETE /api/jobs/:id` | — | Delete job |
| `POST /api/jobs/:id/fitcheck` | — | Fit-check single job via LLM |
| `POST /api/jobs/import-text` | — | Import job from pasted text (AI extracts fields, auto fit-check) |
| `POST /api/scrape/jobs` | — | Scrape jobs.ch for new listings |
| `POST /api/scrape/details` | — | Fetch job detail pages (template_text) |
| `POST /api/fitcheck` | — | Batch fit-check all jobs with `fit_label IS NULL` |
| `GET/POST /api/config` | — | Read / validate + hot-reload config_v2.json |
| `GET/POST /api/config/ai` | — | Read / write AI provider config (provider, endpoint, model, api_key) |
| `GET /api/profile` | — | Read user_profile.md |
| `POST /api/profile/save` | — | Write user_profile.md |
| `POST /api/onboarding/complete` | — | Generate profile from 9 onboarding answers (`answers[0]` = CV text, may be PDF-extracted client-side) |

### Config Shape (`config_v2.json`)
```json
{
  "scrape":   { "queries": [...], "rows": 50 },
  "fitcheck": { "provider": "ollama_local", "model": "...", "endpoint": "...", "limit": 50, "max_tokens": 4000, "temperature": 1.0, "top_p": 0.95, "top_k": 64 }
}
```

AI provider/key are read from `config_v2.json` (`fitcheck.provider`, `fitcheck.endpoint`, `fitcheck.model`) and `config/api_keys.json` (`api_key` field). The `GET/POST /api/config/ai` endpoints abstract both files — prefer them over editing directly.

## 🤖 LLM / Fitcheck

- `buildFitcheckPrompt` lambda substitutes `{{profile}}` / `{{jobText}}` in `config/system_prompt.txt`, loaded once at startup. Missing file or missing placeholders = hard error, server won't start.
- Captured by all 3 fitcheck endpoints: `POST /api/fitcheck` (batch), `POST /api/jobs/:id/fitcheck` (single), `POST /api/admin/fitcheck/recheck/:id` (admin recheck).
- `httpPostAI` has a **600 s timeout** and auto-retries once on empty response or 5xx error (handles Ollama Cloud cold-start).
- `parseStreamingResponse` handles two formats: Ollama native NDJSON and OpenAI-compatible SSE.
- `buildAiRequest(provider, model, prompt, ...)` builds the JSON request body. Provider-specific behavior:
  - `ollama_local`: native API — `"format":"json"`, `top_k` included, no `response_format`
  - `ollama_cloud` / `openrouter` / `mistral`: `"response_format":{"type":"json_object"}`, no `top_k`
  - All others (deepinfra, custom): no JSON mode field
  - All providers: `"stream":false`
- API key gate: all fitcheck/import routes skip the empty-key check when `provider == "ollama_local"`.
- `config_v2` / `config_v2_mutex` (`shared_mutex`): reads use `shared_lock`, writes use `unique_lock`. Snapshot fields before releasing the lock — never hold lock across I/O.

## 🔒 Security

- All user/LLM data injected into `innerHTML` must use `escapeHtml()` — no exceptions.
- `cleanTemplateText`: strips tags **before** entity-decode (prevents `&lt;script&gt;` bypass), then strips again after decode.
- `update_job_field`: whitelists allowed field names — do not expand without review.
- `detail_url` in frontend: only `http(s)://` URLs rendered as links, others fall back to `#`.
- API keys in `config/api_keys.json` (gitignored) — never commit.

## 🗄️ Database

- `db_init()` + `db_v2_init()` — create table + ALTER TABLE for fit columns (idempotent).
- `insert_or_update_job()` — upsert; preserves `company_name` if new value is empty.
- `get_jobs_needing_fitcheck_v2(db, limit)` — `fit_label IS NULL AND template_text IS NOT NULL`.
- `save_fit_result_v2()` — writes `fit_score`, `fit_label`, `fit_summary`, `fit_reasoning`, `fit_profile_hash`.
- `catch (...)` blocks in `db_v2_ensure_tables` are intentional (ALTER TABLE idempotency).

## 🎨 Code Style

**C++:** functions `snake_case`, structs/types `PascalCase`, constants `UPPER_SNAKE_CASE`, locals `snake_case`.

**JavaScript:** functions/variables `camelCase`; exported constants `UPPER_SNAKE_CASE` (URL constants in `api.js`).

### Key Rules
- No unnecessary comments — names should be self-documenting.
- Guard clauses over nested conditionals.
- Handle errors at the level where they can be acted on; don't swallow silently.

## 📝 Commit Guidelines

- Imperative mood: `fix: ...`, `feat: ...`, `refactor: ...`
- Body explains *why*, not what.
- Build must be clean before commit.

---

*Last updated: 2026-04-27 (AI provider settings UI; ollama_local key-gate bypass; provider-aware request building; /api/config/ai endpoints; PDF drop zone on onboarding CV step)*
