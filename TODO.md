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
  Prerequisite: main refactor (steps 8–9 below).
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed


---

## Refactor Plan

Goal: split `main.cpp` into focused files. Behaviour-identical — pure restructure.
Plus three cleanups: thin routes, DRY the AI guards, externalize prompts.

**Status:** file split + service extraction done (steps 1–7). Route split and
main.cpp shrink (steps 8–9) still open.

### Migration steps
- [x] 1. `http.cpp/h` — httpRequest, urlEncode, rateLimitSleep
- [x] 2. `html.cpp/h` — text transforms + cleanTemplateText
- [x] 3. `config.cpp/h` — load/validate, getReadyAi, readApiKey, loadProfileMarkdown (path args, no globals)
- [x] 4. `ai.cpp/h` — buildAiRequest, httpPostAI, parse/extract, FatalAiError, FitcheckResult
- [x] 5. `scraper.cpp/h` — scrapeLinkedIn, jobFromJson, fetchJobDetails
- [x] 6. `app_state.h` — ProgressTracker + AppState; static path globals deleted; api_key now read under mutex
- [x] 7. `fitcheck.cpp/h` — buildFitcheckPrompt (static), runFitcheck, checkAndSave, runBatchFitcheck,
       importJobFromText, generateProfile, generateManualJobId (static), requireAi guard
- [ ] 8. `*_routes.cpp` + `routes.cpp/h` — routes still inline in main.cpp (~540 lines).
       Split into `jobs_routes` / `scrape_routes` / `config_routes` / `fitcheck_routes` / `admin_routes`,
       driven by `registerRoutes(server, state)`. Move `jobRecordToJson` → `jobs_routes.cpp` (static).
- [ ] 9. `main.cpp` → ~60 lines: resolve paths, open db, load config + prompts, populate AppState,
       registerRoutes, listen.

Each remaining step: build green + hit the affected endpoint before moving on.

### Cleanups done
- DRY guards/helpers: `requireAi` (was `resolveAi` in plan; 5 guard copies → 1),
  `checkAndSave` (4 copies → 1).
- `response.cpp/h`: `sendJson` / `sendError` — collapsed ~30 `res.set_content(json…dump())` sites.
- Prompts externalized: `config/onboarding_prompt.txt`, `config/import_prompt.txt`
  (seeded byte-identical from the old inline literals; loaded at startup, fail-fast if missing).
- `/api/jobs` read now takes `db_mutex` (was the unserialized sqlite access).

### Open / separate (do NOT bundle into route split)
- `fitcheck_progress` race — no `compare_exchange` guard like detail-fetch has; two concurrent
  `/api/fitcheck` calls clobber counters. Fix separately.
