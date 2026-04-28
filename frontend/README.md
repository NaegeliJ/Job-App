# Job Radar Frontend

## Overview

Static SPA served by the C++ backend. ES6 modules, no bundler.

> **⚠️ For up-to-date API references and architecture rules**, see `../AGENTS.md`.

## Structure

```
frontend/
├── index.html              # Main SPA
├── onboarding.html         # Onboarding wizard
├── css/
│   ├── variables.css       # All colors (no --text1)
│   ├── base.css
│   ├── layouts/main.css
│   ├── components/         # UI components
│   └── features/           # Feature-specific
└── js/
    ├── main.js             # Entry point
    ├── api.js              # API endpoints
    ├── state.js            # Global state
    ├── utils/
    │   ├── formatting.js   # escapeHtml(), fmtDate()
    │   └── confirm.js      # confirmDialog() — custom modal replace window.confirm()
    └── components/
        ├── header.js       # Search, filters, stats
        ├── job-list.js     # List rendering
        ├── detail.js       # Job detail panel
        ├── actions.js      # User actions & API calls
        ├── modal.js        # Settings modal
        └── console.js      # Dev console (Ctrl+\)
```

## Architecture

- **No build step:** Native ES6 modules.
- **State:** `state.js` is single source of truth.
- **XSS:** All user/LLM content into `innerHTML` must pass `escapeHtml()`.
- **Event wiring:** `main.js` binds everything in `bindEvents()`.

## Keyboard Shortcuts

- **Escape** — closes modals and filter dropdown
- **Ctrl+K** — focus search bar
- **Ctrl+\** — toggle admin console

## Running

```bash
cd /path/to/Job-App/cmake-build-debug
./Job_App
```

Access at: http://localhost:8080

## Development

Edit CSS or JS and reload — no build required.

## Browser Compatibility

Requires modern browser with ES6 modules, CSS custom properties, Fetch API.

Tested on Chrome/Edge 89+, Firefox 88+, Safari 14+.

## Troubleshooting

- **No styles:** Check CSS 404 errors in console.
- **JS errors:** Check import errors in console.
- **API fails:** Verify backend is running.
- **No jobs:** Click "Scrape Jobs" to fetch data.
