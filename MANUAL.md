# Job Master — Manual

## Onboarding

Runs automatically on first launch. Nine questions build your `user_profile.md`:

1. CV — drop a PDF (text-selectable only) or paste plain text
2. Career goal (3–5 years)
3. What drives you
4. Hard no-gos
5. Tech you want to build vs. tolerate
6. Preferred company type and region
7. Hard constraints (salary floor, language, etc.)
8. Work style
9. Anything else the LLM should know

Profile is editable any time via **Profile** (person icon in header). Changes take effect on the next fit-check.

---

## First-time setup

This system uses LLMs, which needs proper instructions and thus, some tuning. 

1. Scrape a small batch (1 query, `rows` set to 5–10).
2. Fit-check those jobs. Read the reasoning.
3. If scores feel off — adjust your profile or LLM settings, clear fit data, recheck.
4. Once results are solid, the system is fully ready. 

---

## Action bar (header)

| Button | What it does |
|--------|--------------|
| **Scrape** | Fetches new listings from jobs.ch per your queries, then auto-fetches full posting text |
| **Fit-Check** | Scores every unscored job via LLM. Progress bar fills left→right. Fatal errors (bad key, no credits, rate limit) stop the batch and turn the button red — fix in Settings, then retry |
| **Add Job manually** | Paste any job posting text. App extracts fields and fit-checks automatically |
| **⊞ Filter** | Filter list by fit label or status: All / Strong / Decent / Exp / Weak / New / Starred / Applied |
| **⇅ Score** | Toggle sort: fit score (default) ↔ date scraped |
| **🗑 Cleanup** | Bulk soft-delete: all Skipped, all No Go, or unseen jobs older than 30 days |

Search bar (top center) filters by title and company in real time.

---

## Auto-deletion

Stale jobs are hard-deleted automatically on every scrape run. No UI action needed.

| Source | Rule |
|--------|------|
| jobs.ch | Deleted when `publication_end_date` has passed |
| LinkedIn | Deleted 60 days after first scraped |

**Exception:** jobs marked **Applied** are never auto-deleted, regardless of source or age. They stay in the database for the [Application tracker](#application-tracker) until you remove them yourself.

Manually imported jobs (Add Job manually) have no LinkedIn age limit; they are only auto-deleted if the AI extracted an end date from the pasted text and that date has passed — and even then not while marked Applied.

Hard-delete = row removed from DB entirely, not recoverable via `restore:all`.

---

## Job detail

Click any job to open the detail panel.

**Fit results**

| Field | Meaning |
|-------|---------|
| Label | Strong / Decent / Experimental / Weak / No Go |
| Score | Weighted 0–100 |
| Summary | One-line verdict |
| Reasoning | Full dimension-by-dimension breakdown |

**Actions**

| Control | Behavior |
|---------|----------|
| ✦ Star | Mark as interested |
| ✓ Applied | Mark as applied |
| ✕ Skip | Dismiss |
| ★ Rating | 1–5 stars, saved immediately |
| Notes | Free text, saved on blur |
| **Recheck** | Re-runs fit-check for this job. Red border = last run failed |
| **Expired** | Marks job as expired and hides it |

Clicking a status button a second time toggles it off.

---

## Application tracker

Open via the **📋 Applied (N)** button in the header — the count includes every application, open or closed. The tracker is a follow-up dashboard listing every job marked **Applied**, on its own page.

Getting jobs in: mark any job as ✓ Applied in the detail panel — scraped or manually imported, both land here. Jobs on the tracker are never auto-deleted (see [Auto-deletion](#auto-deletion)).

Most columns edit inline — click a cell, change it, done. **Position** and **Company** are read-only; the position links back to the job on the dashboard.

| Column | Editable | Notes |
|--------|----------|-------|
| Status | ✔ | Dropdown: Waiting · First interview · Next round · Assessment · Offer · Declined · Withdrawn · Ghosted |
| Position | — | Links to the job's detail panel on the dashboard |
| Company | — | |
| Location | ✔ | Free text, max 200 characters — e.g. to correct an extracted location |
| Applied | ✔ | Date you applied |
| Last reaction | ✔ | Free text, max 500 characters — what the company said |
| Reaction date | ✔ | Date of the company's last response |
| Notes | ✔ | Free text, multiline — the cell grows into a textarea while editing |

Declined, Withdrawn, and Ghosted count as **closed** — greyed out, sorted last, and hidden from the main job list and its counters (they live only here).

**Filter** by status or **Active** (everything not closed). Default **sort** is by progress: Offer first, then Assessment, Next round, First interview, Waiting, closed statuses last; newest reaction breaks ties. Columns are resizable by dragging their edges.

---

## Map view

Open via the **🗺 Map** button in the header. Shows every visible job (same rule as the dashboard: deleted and closed applications stay off) as a colored marker on an OpenStreetMap map of Switzerland.

Positions are resolved in the browser from a bundled Swiss/Liechtenstein place index (`frontend/data/ch-geo.json`) — no external geocoding service, nothing leaves your machine except the map tile requests. Zip code wins when the job has one; otherwise the place name is matched (accent- and spelling-tolerant, e.g. *Zurich*, *Genève 2*, *Biel/Bienne*, *Baden-Daettwil*).

| Element | Behavior |
|---------|----------|
| Marker color | Fit label: Strong (turquoise) · Decent (green) · Experimental (yellow) · Weak (orange) · No Go (dark red) · unscored (gray) |
| Clusters | Nearby jobs group into a numbered circle; zoom or click to expand. Jobs sharing one zip fan out at max zoom |
| Popup | Title (links back to the job on the dashboard), company, fit score |
| Filter chips | Same fit-label/status filters as the dashboard |
| **⌂ Set home** | Arm, then click the map to place your home. Stored in the browser (per device), shown with a radius circle |
| **📍** | Set home from browser geolocation — only works on `localhost`; on a LAN address the browser blocks it, use ⌂ instead |
| Radius slider | Appears once home is set. Hides all jobs beyond the chosen distance (5–100 km, as the crow flies) |
| Bottom drawer | Jobs whose location can't be pinned (e.g. "Switzerland", canton-only, empty) — listed there, never silently dropped |

Remove home via the popup on the home marker.

The place index is generated by `scripts/build-geo-json.mjs` (regeneration command in its header). Roughly 95% of real jobs resolve; the rest land in the drawer.

---

## Profile

Open via the person icon in the header. Plain Markdown — write exactly what you want the LLM to know about you. Save with the **Save** button. No restart needed.

---

## Settings

Open via the gear icon. Two sections, both hot-reload (no restart):

Saving AI provider settings first runs a live connection test against the endpoint (60 s timeout). If the test fails — wrong key, unreachable endpoint, bad model — the save is blocked and the error is shown, so a broken config never reaches the fit-check. The same test runs during onboarding.

**AI provider**

| Field | Notes |
|-------|-------|
| Provider | `ollama_local` · `ollama_cloud` · `openrouter` · `deepinfra` · `mistral` · `custom` |
| Endpoint | Full HTTP URL to the chat completions endpoint |
| Model | Model identifier as the provider expects it |
| API key | Leave empty for local Ollama |

**Scrape & fit-check**

| Field | Notes |
|-------|-------|
| Queries | One search string per line, sent to jobs.ch |
| Rows | Max listings fetched per query |
| Limit | Max jobs per batch fit-check call |
| Max tokens | Cap on LLM response length |
| Temperature / top_p / top_k | Sampling parameters |

**LinkedIn (optional, off by default)**

Scrapes LinkedIn's public guest job-search page — no login or API key needed. Because it's unofficial (HTML scraping, not a real API), it can break if LinkedIn changes markup, and is rate-limited harder than jobs.ch (built-in backoff on HTTP 429).

| Field | Notes |
|-------|-------|
| Queries | One search string per line |
| Location | e.g. `Switzerland` |
| Time range | LinkedIn's `f_TPR` param, e.g. `r604800` = last 7 days |
| Max results | Per query, clamped 1–50 (default 25) — this is the app's own cap, not a limit LinkedIn publishes |

**Automode**

Runs the full pipeline (scrape all enabled sources → fetch details → batch fit-check) on a timer in the background, using whatever AI provider and Scrape/LinkedIn settings are currently saved — not a frozen snapshot from onboarding. Change Settings any time and the next automode run picks it up (checked every 5s, hot-reload, no restart).

| Field | Notes |
|-------|-------|
| Enabled | Off by default |
| Interval (hours) | Minimum 1. Timer resets after each run, not wall-clock aligned |

If the AI provider isn't reachable or the profile is empty, a run is skipped silently (logged server-side only, no UI error).

---

## Admin console

`Ctrl+\` toggles the console. All commands accept partial job IDs (last 8 characters). Type `help` for the full reference.

**Delete / restore**

| Command | Effect |
|---------|--------|
| `delete:job <id>` | Soft-delete one job (hidden, restorable) |
| `delete:status <status>` | Soft-delete all jobs with given status |
| `delete:label <label>` | Soft-delete all with given fit label |
| `restore:all` | Restore all soft-deleted jobs to unseen |
| `purge:job <id>` | Permanent hard-delete of one job |
| `purge:label <label>` | Permanent hard-delete of all with given fit label |

**Fit-check**

| Command | Effect |
|---------|--------|
| `fitcheck:clear <id>` | Clear fit data for one job |
| `fitcheck:clear-all` | Clear fit data for all jobs |
| `fitcheck:recheck <id>` | Clear + recheck one job |
| `fitcheck:recheck-all` | Clear all fit data and queue full batch recheck |

---

## Docker: pinning a version

`setup.sh` and `update.sh` always track `ghcr.io/meisdy/job-app:latest`. To run a specific release instead:

1. Edit `docker-compose.yml`, change `image: ghcr.io/meisdy/job-app:latest` to `image: ghcr.io/meisdy/job-app:vX.Y.Z`.
2. `docker compose pull && docker compose up -d`

Note: `update.sh` will overwrite this back to `latest` on next run — re-pin after updating if you need to stay pinned.

---

## Keyboard shortcuts

| Shortcut             | Action |
|----------------------|--------|
| `Ctrl+\` (US layout) | Toggle admin console |
| `Ctrl+K`             | Focus search bar |
| `Escape`             | Close open modal / dropdown |
