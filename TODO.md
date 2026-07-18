# TODO

## UX improvements
- [ ] DB View optimisation for mobile view
- [ ] Overall simplify UI

## Logic
- [x] Periodic auto-scrape + fit-check (Available for testing now)
- [x] Onboarding should test API call before user fills out fields
- [x] Investigate bug .exe not working when downloading directly
- [ ] Restore single job? Generally admin console features and if it shall remain at all
- [ ] Onboarding questions not optimal, also tedious to fill out. Maybe voice input or pick questions / answers
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed
- [ ] Bug: after scraping and fitcheck running in background through automode, all skipped jobs reset for some reason. Not reproducible yet. 



## Performance
- [ ] `/api/jobs` sends every column of every job (~13 MB, 2386 rows). Return list columns only; add `/api/jobs/:id/detail` for `fit_reasoning`, `fit_summary`, `template_text`, fetched when a job is opened



## Ideas
- Data sources and more scraping
- Adding a general AI ask me questions logic to ask questions like, does A fit better than B, or maybe even adjust rating due to reason X.
- Reminder system and generally full pipeline from searching to apply overview, reminders and more
- .exe compilation probably not needed. But this results in a general user use-case overhaul


## The Project and its main Issue

Project

Self-hosted job application tool. Raspberry Pi, SQLite, AGPL-3.0. Two users: you and a friend. Built for your own use; not laymen-ready.

Two parts:
- Ingest — mass scrapes jobs.ch and LinkedIn into the DB.
- Pipeline — save, rate, fitcheck, apply notes.

Known facts

- jobs.ch: robots.txt disallows the endpoint in use. No guards against mass scraping beyond DDoS protection.
- LinkedIn: scraped via public guest pages, no account. 25 results per query currently.
- LinkedIn carries jobs found nowhere else, and higher quality than alternatives.
- Combined ingest surfaces more jobs than professional Swiss job agencies.
- Pipeline is standalone valuable — usable with no ingest, rating jobs the user encounters. Mass data is a multiplier on it, not a prerequisite.
- Scraping is not sellable. Pipeline is.
- Centralised ingest amortises: one scrape serves any number of users.
- /api/jobs measured 1.80s TTFB, 5.42s total, 13.1MB before the -O3+gzip and soft-delete patches. Not re-measured since.

The distribution problem

Both data sources are unlicensed and used against their ToS. No arrangement discussed removes this while keeping coverage:

┌─────────────────────────┬────────┬──────────┬──────────────────────┐
│      Configuration      │ Simple │ Coverage │ Uses unlicensed data │
├─────────────────────────┼────────┼──────────┼──────────────────────┤
│ Your server scrapes     │             │
├─────────────────────────┼────────┼──────────┼──────────────────────┤
│ Pipeline only, user-fed │             │
├─────────────────────────┼────────┼──────────┼──────────────────────┤
│ Client-side mass ingest │ ❌     │ ✅       │ Yes — the user       │
├─────────────────────────┼─────────────┤
│ Official APIs only      │ ✅     │ ❌       │ No                   │
└─────────────────────────┴────────┴──────────┴──────────────────────┘

Client-side additionally needs the user's LinkedIn account and their browser open.

The legal configurations areow what the tool currently is for you. The gap between them and the current tool is the mass ingest.

Not known

- Whether JobCloud would license jobs.ch access. Never asked.
- Coverage loss if LinkedIn were dropped — no number.
- Whether a LinkedIn account
- Your actual legal exposure, at any of these configurations. Everything I said about Swiss database rights, UWG, DSG and US case law was reasoning from general knowledge, not verified for your situation. It needs a lawyer, and the answer plausibly decides which configurations are ope

## Direction / Way Forward

Every big decision (data model, license, money, JobCloud pitch) is blocked on missing info — coverage numbers, legal exposure, a roadmap to pitch. But one thing is valuable under *every* outcome and blocked by *none* of the unknowns: making the pipeline production-ready (installable by a stranger). It's required for OSS release, for a pitch, for a side hustle, and for the portfolio to not look like a toy. So it's not blocked — it's the actual next step. Undecided things have been stalling the decided thing.

Wants, mapped:

- **Help people, not corporates** — AGPL already prevents closed-source corporate takeover. To go further: PolyForm Noncommercial or dual-license (individuals free, companies pay). Decide later; blocks nothing now.
- **Get something back beyond "it exists"** — the lackluster feeling is real because nobody but you touches it. The return is a stranger using it and saying so, which outweighs stars. Requires installable-by-strangers. Same unblock.
- **Legally okay** — already green for current use (self-host, own use). Stays green as long as you don't host scraping for others. Ship pipeline + legal adapters only; scrapers stay private setup.
- **Side hustle** — hardest, needs most info, decide last. Production-readiness is a prerequisite anyway. Build first, choose money model once users + coverage numbers exist.

Sequence (no unknowns required):

1. Harden pipeline to installable-by-a-stranger. Close the "not laymen-ready" gap: config, docs, setup.
2. Ship with legal adapters only (job-room, career pages, official APIs). Scrapers stay private plugins.
3. Release AGPL or PolyForm-NC. Real README, one screenshot, honest scope.
4. Coverage benchmarking now has a point — real users hit real gaps and report which sources they miss. The tedious test becomes their bug reports.
5. With users + coverage data + working product, the JobCloud pitch writes itself and the money-model question has answers.

Reframe: deciding the endgame before the opening. Every named unknown gets cheaper to answer *after* steps 1–3, none before. Stop deciding, ship the pipeline, let real usage resolve the unknowns.
