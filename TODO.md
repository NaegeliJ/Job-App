# TODO

## UX improvements
- [x] Scrape button is finished before detail fetch is

## Logic
- [x] No-Go label can have higher points than weak
- [ ] No Fitcheck Done log for single fitcheck
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

Goal: clean `main.cpp` (1868 lines, does everything) into focused files. No behaviour change.

### Phase 1 — Quick wins in-place (no file split yet)

- [x] **Unify progress struct** — rename `FitcheckProgress` → `ProgressTracker`, replace anonymous `detail_progress` struct with same type (`main.cpp:742-754`)
- [x] **Extract shared whitespace-collapse logic** — `cleanHtmlField` and `cleanTemplateText` duplicate the strip+collapse loop; make `cleanTemplateText` call `cleanHtmlField` internally (`main.cpp:207`, `533`)
- [x] **Free function `generateManualJobId`** — remove lambda-in-main, hoist to free function (`main.cpp:1551`)
- [x] **Use `buildAiRequest` in onboarding handler** — `/api/onboarding/complete` builds JSON inline; use existing helper (`main.cpp:1331`)
- [x] **Fix stale error string** — `"no content jobFields"` in `parseStreamingResponse` (`main.cpp:644`)
- [x] **Remove redundant `set_header`** in `GET /api/profile` handler (`main.cpp:1377`)
- [x] **Drop what-comments in `urlEncode`** (`main.cpp:85-87`)
- [x] **Rename `db_write_mutex` → `db_mutex`** — used for reads too; name is wrong (`main.cpp:738`)

### Phase 2 — Correctness / thread safety

- [x] **Guard concurrent `/api/scrape/details`** — reset progress only if `!detail_progress.running`; return 409 if already running (`main.cpp:969`)
- [x] **Lock `api_key` on reads** — `api_key` captured by ref in route lambdas, read without `api_key_mutex`; all reads must lock (`main.cpp:various`)
- [x] **Rename `httpGetLinkedInPublic` → `httpGetLinkedInSearch`** — name now matches usage (search scrape vs. detail fetch)

### Phase 3 — File split (prerequisite for scheduler feature)

Target layout:
```
src/
  main.cpp          — startup only: init db, load config, register routes, listen
  routes.cpp        — all server.Get / server.Post / server.Delete handlers
  routes.h
  ai.cpp            — buildAiRequest, httpPostAI, runFitcheck, parseStreamingResponse, extractJsonFromResponse
  ai.h
  scraper.cpp       — scrapeLinkedIn, scrapeJobsCh, detail-fetch logic
  scraper.h
  html.cpp          — stripHtmlTags, decodeHtmlEntities, cleanHtmlField, cleanTemplateText, extractTagContent, urlEncode
  html.h
  config.cpp        — ConfigV2, loadConfigV2, validateConfigV2, AiSnapshot, snapshotAiConfig, getReadyAi
  config.h
```

Order: html → config → ai → scraper → routes → main. Each step compiles and passes tests before next.
