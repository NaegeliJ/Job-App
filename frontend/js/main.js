import state from './state.js';
import { GET_URL, PROFILE_GET_URL, VERSION_URL } from './api.js';
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

    // Deep link from tracker: /?job=<id> opens the job detail
    const linkedJobId = new URLSearchParams(window.location.search).get('job');
    if (linkedJobId) selectJob(linkedJobId);
  } catch (e) {
    console.error('Init error:', e);
    setConnectionStatus('error');
    const jobList = document.getElementById('job-list');
    if (jobList) {
      jobList.innerHTML = '<div class="ldw" style="color:var(--red)">Connection failed</div>';
    }
  }
}

function onClick(id, handler) {
  const el = document.getElementById(id);
  if (el) el.addEventListener('click', handler);
}

function bindEvents() {
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

  document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', () => setFilter(btn, btn.dataset.filter));
  });

  const searchInput = document.getElementById('search-input');
  if (searchInput) searchInput.addEventListener('input', onSearch);
  onClick('search-clear', clearSearch);

  onClick('scrape-btn', scrapeJobs);
  onClick('import-job-btn', openImportModal);
  onClick('profile-btn', openProfile);
  onClick('profile-close', closeProfile);
  onClick('profile-cancel-btn', closeProfile);
  onClick('profile-save-btn', saveProfile);
  onClick('profile-redo-btn', openOnboarding);
  onClick('onboard-btn', openOnboarding);
  onClick('fitcheck-btn', triggerFitCheck);
  onClick('map-btn', () => { window.location.href = '/map.html'; });
  onClick('tracker-btn', () => { window.location.href = '/tracker.html'; });
  onClick('settings-btn', openSettings);
  onClick('sort-btn', toggleSort);

  initBulkDeleteDropdown();

  const jobList = document.getElementById('job-list');
  if (jobList) {
    jobList.addEventListener('click', e => {
      const item = e.target.closest('.job-item');
      if (!item) return;
      const id = item.dataset.id;
      if (id) selectJob(id);
    });
  }

  onClick('btn-i', () => setStatus('interested'));
  onClick('btn-a', () => setStatus('applied'));
  onClick('btn-s', () => setStatus('skipped'));
  onClick('btn-e', setExpired);

  onClick('modal-close', closeSettings);
  onClick('modal-cancel-btn', closeSettings);
  onClick('modal-save-btn', saveSettings);

  onClick('import-close', closeImportModal);
  onClick('import-cancel-btn', closeImportModal);
  onClick('import-btn', importJobFromText);
  onClick('import-url-save-btn', saveImportUrl);
  onClick('import-url-skip-btn', closeImportModal);

  const modalOverlay = document.getElementById('settings-overlay');
  if (modalOverlay) {
    modalOverlay.addEventListener('mousedown', e => { state._modalMousedownTarget = e.target; });
    modalOverlay.addEventListener('click', e => {
      if (e.target === modalOverlay && state._modalMousedownTarget === modalOverlay) closeSettings();
    });
  }

  const profileOverlay = document.getElementById('profile-overlay');
  if (profileOverlay) {
    profileOverlay.addEventListener('mousedown', e => { state._profileModalMousedownTarget = e.target; });
    profileOverlay.addEventListener('click', e => {
      if (e.target === profileOverlay && state._profileModalMousedownTarget === profileOverlay) closeProfile();
    });
  }

  const importOverlay = document.getElementById('import-overlay');
  if (importOverlay) {
    importOverlay.addEventListener('mousedown', e => { state._importModalMousedownTarget = e.target; });
    importOverlay.addEventListener('click', e => {
      if (e.target === importOverlay && state._importModalMousedownTarget === importOverlay) closeImportModal();
    });
  }

  const detailScroll = document.getElementById('detail-scroll');
  if (detailScroll) {
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

async function checkForUpdate() {
  try {
    const [ver, release] = await Promise.all([
      fetch(VERSION_URL).then(r => r.json()),
      fetch('https://api.github.com/repos/Meisdy/Job-App/releases/latest').then(r => r.json())
    ]);
    const current = ver.version;
    const latest = release.tag_name;
    if (!latest || !current || !/^v?\d+\.\d+/.test(current)) return;
    if (current !== latest && current !== latest.replace(/^v/, '')) {
      const notice = document.getElementById('update-notice');
      if (notice) {
        notice.textContent = `↑ ${latest} — run: bash update.sh`;
        notice.style.display = '';
      }
    }
  } catch {}
}

document.addEventListener('DOMContentLoaded', () => {
  init();
  initConsole();
  checkForUpdate();
});

export { init, bindEvents };