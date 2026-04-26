import state from './state.js';
import { GET_URL, PROFILE_GET_URL } from './api.js';
import { setConnectionStatus, updateStats, onSearch, clearSearch, setFilter, toggleSort, initBulkDeleteDropdown } from './components/header.js';
import { renderList, selectJob } from './components/job-list.js';
import { closeSettings, openSettings, saveSettings } from './components/modal.js';
import { setStatus, setRating, hoverStar, unhoverStar, setExpired, saveNotes, scrapeJobs, triggerFitCheck, openProfile, closeProfile, saveProfile, openOnboarding, importJobFromText, saveImportUrl, openImportModal, closeImportModal } from './components/actions.js';
import { initConsole, toggleConsole } from './components/console.js';

async function init() {
  const profileRes = await fetch(PROFILE_GET_URL);
  if (profileRes.status === 404) {
    window.location.href = '/onboarding.html';
    return;
  }

  setConnectionStatus('loading');
  try {
    const r = await fetch(GET_URL);
    state.allJobs = await r.json();
    state.allJobs.sort((a, b) => (b.fit_score || 0) - (a.fit_score || 0));
    setConnectionStatus('connected');
    updateStats();
    renderList();
    bindEvents();
  } catch (e) {
    console.error('Init error:', e);
    setConnectionStatus('error');
    const jobList = document.getElementById('job-list');
    if (jobList) {
      jobList.innerHTML = '<div class="ldw" style="color:var(--red)">Connection failed</div>';
    }
  }
}

// Helper to safely add event listener
function onClick(id, handler) {
  const el = document.getElementById(id);
  if (el) el.addEventListener('click', handler);
}

// Bind all UI events
function bindEvents() {
  // Filter dropdown toggle
  const filterDropdownBtn = document.getElementById('filter-dropdown-btn');
  const filterDropdownMenu = document.getElementById('filter-dropdown-menu');
  if (filterDropdownBtn && filterDropdownMenu) {
    filterDropdownBtn.addEventListener('click', e => {
      e.stopPropagation();
      const isOpen = filterDropdownMenu.classList.contains('open');
      filterDropdownMenu.classList.toggle('open', !isOpen);
      filterDropdownBtn.setAttribute('aria-expanded', String(!isOpen));
    });
    document.addEventListener('click', () => {
      filterDropdownMenu.classList.remove('open');
      filterDropdownBtn.setAttribute('aria-expanded', 'false');
    });
  }

  // Filter buttons (inside dropdown)
  document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      setFilter(btn, btn.dataset.filter);
    });
  });

  // Search
  const searchInput = document.getElementById('search-input');
  if (searchInput) {
    searchInput.addEventListener('input', onSearch);
  }
  onClick('search-clear', clearSearch);

  // Tool buttons
  onClick('scrape-btn', scrapeJobs);
  onClick('import-job-btn', openImportModal);
  onClick('profile-btn', openProfile);
  onClick('profile-close', closeProfile);
  onClick('profile-cancel-btn', closeProfile);
  onClick('profile-save-btn', saveProfile);
  onClick('profile-redo-btn', openOnboarding);
  onClick('onboard-btn', openOnboarding);
  onClick('fitcheck-btn', triggerFitCheck);
  onClick('settings-btn', openSettings);

  // Sort button
  onClick('sort-btn', toggleSort);

  // Bulk delete dropdown
  initBulkDeleteDropdown();

  // Job list - event delegation
  const jobList = document.getElementById('job-list');
  if (jobList) {
    jobList.addEventListener('click', e => {
      const item = e.target.closest('.job-item');
      if (!item) return;
      const id = item.dataset.id;
      if (id) selectJob(id);
    });
  }

  // Action bar buttons
  onClick('btn-i', () => setStatus('interested'));
  onClick('btn-a', () => setStatus('applied'));
  onClick('btn-s', () => setStatus('skipped'));
  onClick('btn-e', setExpired);

  // Modal buttons
  onClick('modal-close', closeSettings);
  onClick('modal-cancel-btn', closeSettings);
  onClick('modal-save-btn', saveSettings);

  // Import modal buttons
  onClick('import-close', closeImportModal);
  onClick('import-cancel-btn', closeImportModal);
  onClick('import-btn', importJobFromText);
  onClick('import-url-save-btn', saveImportUrl);
  onClick('import-url-skip-btn', closeImportModal);

  // Modal overlay click
  const modalOverlay = document.getElementById('settings-overlay');
  if (modalOverlay) {
    modalOverlay.addEventListener('mousedown', e => {
      state._modalMousedownTarget = e.target;
    });
    modalOverlay.addEventListener('click', e => {
      if (e.target === modalOverlay && state._modalMousedownTarget === modalOverlay) {
        closeSettings();
      }
    });
  }

  // Profile modal overlay click
  const profileOverlay = document.getElementById('profile-overlay');
  if (profileOverlay) {
    profileOverlay.addEventListener('mousedown', e => { state._profileModalMousedownTarget = e.target; });
    profileOverlay.addEventListener('click', e => {
      if (e.target === profileOverlay && state._profileModalMousedownTarget === profileOverlay) closeProfile();
    });
  }

  // Import modal overlay click
  const importOverlay = document.getElementById('import-overlay');
  if (importOverlay) {
    importOverlay.addEventListener('mousedown', e => {
      state._importModalMousedownTarget = e.target;
    });
    importOverlay.addEventListener('click', e => {
      if (e.target === importOverlay && state._importModalMousedownTarget === importOverlay) {
        closeImportModal();
      }
    });
  }

  // Detail panel - event delegation for dynamic content
  const detailScroll = document.getElementById('detail-scroll');
  if (detailScroll) {
    // Star ratings and save notes
    detailScroll.addEventListener('click', e => {
      const star = e.target.closest('.star');
      if (star) {
        const rating = parseInt(star.dataset.rating);
        if (!isNaN(rating)) setRating(rating);
      }
      const saveBtn = e.target.closest('#save-notes-btn');
      if (saveBtn) saveNotes();
    });

    detailScroll.addEventListener('mouseenter', e => {
      const star = e.target.closest('.star');
      if (star) {
        const rating = parseInt(star.dataset.rating);
        if (!isNaN(rating)) hoverStar(rating);
      }
    }, true);

    detailScroll.addEventListener('mouseleave', e => {
      const star = e.target.closest('.star');
      if (star) unhoverStar();
    }, true);
  }
}

// Keyboard listeners
document.addEventListener('keydown', e => {
  if (e.key === 'Escape') {
    closeSettings();
    closeProfile();
    closeImportModal();
    const menu = document.getElementById('filter-dropdown-menu');
    const btn = document.getElementById('filter-dropdown-btn');
    if (menu) menu.classList.remove('open');
    if (btn) btn.setAttribute('aria-expanded', 'false');
  }
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault();
    const searchInput = document.getElementById('search-input');
    if (searchInput) {
      searchInput.focus();
      searchInput.select();
    }
  }
});

// Initialize app on DOMContentLoaded
document.addEventListener('DOMContentLoaded', () => {
  init();
  initConsole();
});

export { init, bindEvents };