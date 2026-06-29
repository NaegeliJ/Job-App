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

Goal: clean `main.cpp` (1868 lines, does everything) into focused files. No behaviour change.

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
