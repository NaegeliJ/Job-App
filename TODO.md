# TODO

## UX improvements
- [x] Scrape button is finished before detail fetch is

## Logic
- [x] No-Go label can have higher points than weak
- [x] No Fitcheck Done log for single fitcheck
- [ ] Periodic auto-scrape + fit-check
  Plan: background `std::jthread` + `condition_variable`, wakes on interval or config change.
  Sequence: scrape jobs → fetch details (poll until done) → batch fitcheck.
  Config fields (in ConfigV2 + config_v2.json): `periodic_enabled`, `periodic_interval_minutes`, `periodic_run_fitcheck`.
  New files: `include/scheduler.h`, `src/scheduler.cpp`.
  Expose state via `GET /api/scheduler/status`, manual trigger via `POST /api/scheduler/trigger`.
  UI: enable toggle + interval input + last/next run display + "Run now" button.
  Prerequisite: main refactor.
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed


---

## Refactor Plan

Goal: split `main.cpp` (1863 lines, does everything) into focused files. Behaviour-identical
— pure restructure. Beyond the file split, three cleanups: thin routes (no logic in lambdas),
DRY the repeated AI guards, externalize embedded prompts.

### Key design decisions
- No classes for AI/scraper/config/fitcheck — free functions only, no polymorphism needed.
- `AppState` struct solves route lambda-capture pollution (6–8 captured vars → `[&state]`).
  Holds: db, db_mutex, config_v2, config_v2_mutex, api_key, api_key_mutex, progress trackers,
  system_prompt_template, resolved paths.
- `FatalAiError` stays a class (derives `std::runtime_error`) — the one real exception type.
- Future `Scheduler` will be a class — thread + condvar + RAII start/stop lifecycle.
- Static path globals (`base_dir`, `s_config_path`, etc.) die — become `AppState` fields.
  Every function that called `configPath()` / `profilePath()` takes a path argument instead.
- `ProgressTracker` → `app_state.h` (field of AppState only).
- `FitcheckResult` → `ai.h` (return type of `runFitcheck`).
- `jobRecordToJson` → `jobs_routes.cpp` static (only routes need it).
- `jobFromJson` → `scraper.cpp/h` (all callers are scraper paths; avoids JSON dep in db.h).
- `cleanTemplateText` → `html.cpp` (json::parse stays in the .cpp; `html.h` decl is string→string).

### Thin routes — fat lambda bodies become named service functions
Route lambda does one thing: parse → call service → serialize. Logic moves out:
- detail-fetch thread loop → `fetchJobDetails(state)`        in `scraper.cpp`
- batch fitcheck loop      → `runBatchFitcheck(state)`       in `fitcheck.cpp`
- import-text             → `importJobFromText(state, text)` in `fitcheck.cpp`
- onboarding             → `generateProfile(state, answers)` in `fitcheck.cpp`

### DRY helpers (collapse repeated patterns)
- `resolveAi(state, res) -> std::optional<AiSnapshot>` — guard + write 500 on fail. Kills 5 copies.
- `checkAndSave(state, job_id, cleaned, profile, hash)` — `runFitcheck` + `save_fit_result_v2`. Kills 4 copies.

### Prompts out of code → config files
Big embedded prompt literals leave the handler bodies (matches existing config-survives-rebuild pattern):
- onboarding profile template → `config/onboarding_prompt.txt`
- import field-extraction prompt → `config/import_prompt.txt`
Loaded once at startup like `system_prompt.txt`; fail fast if missing. Seed both from the current
literals in the same commit so existing deploys don't break.

### Target layout
```
include/
  db.h          — Job, JobRecord, DB function decls            [exists, no JSON dep]
  http.h        — httpRequest, urlEncode, rateLimitSleep       [NEW — shared by ai + scraper]
  html.h        — decode/strip/collapse, cleanHtmlField, extractTagContent,
                   findAllCaptures, cleanTemplateText           [string→string only]
  config.h      — ConfigV2, AiSnapshot, loadConfigV2, validateConfigV2,
                   snapshotAiConfig, getReadyAi, readApiKey, loadProfileMarkdown(path)
  ai.h          — FatalAiError, FitcheckResult, buildAiRequest, httpPostAI,
                   parseStreamingResponse, extractJsonFromResponse, extractBlock
  fitcheck.h    — runFitcheck, checkAndSave, runBatchFitcheck, importJobFromText, generateProfile
  scraper.h     — scrapeLinkedIn, jobFromJson, fetchJobDetails
  app_state.h   — ProgressTracker, AppState struct
  routes.h      — registerRoutes(server, state)

src/
  main.cpp           — ~60 lines: resolve paths, open db, load config + prompts,
                        populate AppState, registerRoutes, listen
  db.cpp             — unchanged
  http.cpp           — httpRequest, writeCallback (static), urlEncode, rateLimitSleep
  html.cpp           — all HTML/text transforms + cleanTemplateText
  config.cpp         — load/validate, snapshotAiConfig, getReadyAi, apiKeyReady (static),
                        readApiKey, loadProfileMarkdown(path)
  ai.cpp             — classifyAiError (static), isOllamaLocal/supportsJsonMode (static),
                        buildAiRequest, httpPostAI, parseStreamingResponse,
                        extractJsonFromResponse, extractBlock
  scraper.cpp        — httpGet/httpGetLinkedIn/httpGetLinkedInSearch (static),
                        parseLinkedInPubDate/Grade (static), scrapeLinkedIn, jobFromJson,
                        fetchJobDetails
  fitcheck.cpp       — buildFitcheckPrompt (static), runFitcheck, checkAndSave,
                        runBatchFitcheck, importJobFromText, generateProfile,
                        generateManualJobId (static), resolveAi
  routes.cpp         — registerRoutes → calls the 5 register* fns below
  jobs_routes.cpp    — job CRUD/list/bulk handlers + jobRecordToJson (static)
  scrape_routes.cpp  — /api/scrape/* handlers
  config_routes.cpp  — /api/config*, /api/profile* handlers
  fitcheck_routes.cpp— /api/fitcheck*, import-text, onboarding handlers
  admin_routes.cpp   — /api/admin/* handlers
```

### Migration order (compile + smoke-test each step)
1. `http.cpp/h` — zero deps; unblocks ai and scraper
2. `html.cpp/h` — zero deps, pure functions
3. `config.cpp/h` — functions take explicit path args, not static globals
4. `ai.cpp/h` — depends on `http.h`, `config.h`
5. `scraper.cpp/h` — depends on `http.h`, `html.h`, `config.h`, `db.h`; includes `fetchJobDetails`
6. `app_state.h` — depends on `config.h`, `db.h`; defines `ProgressTracker` inline
7. `fitcheck.cpp/h` — depends on `ai.h`, `config.h`, `db.h`, `app_state.h`; service fns + DRY helpers
8. `*_routes.cpp` + `routes.cpp/h` — thin handlers; depend on everything
9. `main.cpp` — shrinks to ~60 lines; static path globals deleted; loads new prompt files

Each step: build green + hit the affected endpoint before moving on.

### Out of scope (do NOT bundle into this refactor)
- `fitcheck_progress` race — no `compare_exchange` guard like detail-fetch has; two concurrent
  `/api/fitcheck` calls clobber counters. Fix separately.
- Periodic auto-scrape scheduler (see Logic section) — prerequisite is this refactor.
