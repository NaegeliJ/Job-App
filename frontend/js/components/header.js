import state from '../state.js';
import { renderList } from './job-list.js';
import { showConfirm } from '../utils/confirm.js';

// ============================================================================
// Connection Status
// ============================================================================

export function setConnectionStatus(status) {
  const dot = document.getElementById('status-dot');
  const label = document.getElementById('status-label');
  
  if (!dot || !label) return;
  
  const statusMap = {
    connected: { className: 'status-dot connected', text: 'Live' },
    error: { className: 'status-dot error', text: 'Offline' },
    loading: { className: 'status-dot', text: '...' }
  };
  
  const config = statusMap[status] || statusMap.loading;
  dot.className = config.className;
  label.textContent = config.text;
}

// ============================================================================
// Search
// ============================================================================

export function onSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (!searchInput) return;
  
  state.searchQuery = searchInput.value.toLowerCase();
  
  if (clearButton) {
    clearButton.style.display = state.searchQuery ? 'block' : 'none';
  }
  
  renderList();
}

export function clearSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (searchInput) searchInput.value = '';
  if (clearButton) clearButton.style.display = 'none';
  
  state.searchQuery = '';
  renderList();
}

// ============================================================================
// Stats Update
// ============================================================================

function countByFitLabel(jobs, label) {
  return jobs.filter(job => job.fit_label === label).length;
}

function countByUserStatus(jobs, status) {
  if (status === 'unseen') {
    return jobs.filter(job => !job.user_status || job.user_status === 'unseen').length;
  }
  return jobs.filter(job => job.user_status === status).length;
}

function countWeakJobs(jobs) {
  return jobs.filter(job => job.fit_label === 'Weak' || job.fit_label === 'No Go').length;
}

export function updateStats() {
  const jobs = state.allJobs.filter(j => j.user_status !== 'deleted');
  const total = jobs.length;
  
  // Fit verdict counts
  const counts = {
    strong: countByFitLabel(jobs, 'Strong'),
    decent: countByFitLabel(jobs, 'Decent'),
    experimental: countByFitLabel(jobs, 'Experimental'),
    weak: countWeakJobs(jobs),
    unseen: countByUserStatus(jobs, 'unseen'),
    interested: countByUserStatus(jobs, 'interested'),
    applied: countByUserStatus(jobs, 'applied')
  };
  
  // Update filter buttons
  const buttons = {
    'filter-all': `All (${total})`,
    'filter-strong': `Strong (${counts.strong})`,
    'filter-decent': `Decent (${counts.decent})`,
    'filter-experimental': `Exp (${counts.experimental})`,
    'filter-weak': `Weak (${counts.weak})`,
    'filter-unseen': `New (${counts.unseen})`,
    'filter-interested': `Starred (${counts.interested})`,
    'filter-applied': `Applied (${counts.applied})`
  };
  
  Object.entries(buttons).forEach(([id, text]) => {
    const btn = document.getElementById(id);
    if (btn) btn.textContent = text;
  });
}

// ============================================================================
// Filter & Sort
// ============================================================================

const filterLabels = {
  all: 'ALL', strong: 'STRONG', decent: 'DECENT',
  experimental: 'EXP', weak: 'WEAK', unseen: 'NEW',
  interested: 'STARRED', applied: 'APPLIED'
};

export function setFilter(button, filterName) {
  state.currentFilter = filterName;

  document.querySelectorAll('.filter-btn').forEach(btn => btn.classList.remove('active'));
  button.classList.add('active');

  const dropdownBtn = document.getElementById('filter-dropdown-btn');
  if (dropdownBtn) {
    dropdownBtn.textContent = `⊞ ${filterLabels[filterName] ?? filterName.toUpperCase()}`;
    dropdownBtn.setAttribute('aria-expanded', 'false');
  }

  const menu = document.getElementById('filter-dropdown-menu');
  if (menu) menu.classList.remove('open');

  renderList();
}

export function toggleSort() {
  const sortButton = document.getElementById('sort-btn');
  if (!sortButton) return;

  // Toggle sort mode
  state.sortMode = state.sortMode === 'score' ? 'date' : 'score';

  // Update button appearance
  const isDateSort = state.sortMode === 'date';
  sortButton.textContent = isDateSort ? '⇅ DATE' : '⇅ SCORE';
  sortButton.style.color = isDateSort ? 'var(--accent)' : 'var(--text3)';
  sortButton.style.borderColor = isDateSort ? 'rgba(96,165,250,0.4)' : 'var(--border2)';

  renderList();
}

// ============================================================================
// Bulk Delete Dropdown
// ============================================================================

function isOlderThan30Days(scrapedAt) {
  if (!scrapedAt) return false;
  const cutoff = new Date();
  cutoff.setDate(cutoff.getDate() - 30);
  return new Date(scrapedAt) < cutoff;
}

async function bulkDeleteRequest(body) {
  const res = await fetch('/api/jobs/bulk', {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.detail || 'Bulk delete failed');
  }
  const data = await res.json();
  state.allJobs = await fetch('/api/jobs').then(r => r.json());
  renderList();
  updateStats();
  return data.deleted;
}

function toastBulk(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;
  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2000);
}

export function updateBulkDeleteMenu() {
  const menu = document.getElementById('bulk-delete-menu');
  if (!menu) return;

  const active = state.allJobs.filter(j => j.user_status !== 'deleted');
  const skippedCount = active.filter(j => j.user_status === 'skipped').length;
  const noGoCount = active.filter(j => (j.fit_label || '').toLowerCase() === 'no go').length;
  const oldUnseenCount = active.filter(j => (!j.user_status || j.user_status === 'unseen') && isOlderThan30Days(j.scraped_at)).length;

  const items = [];
  if (skippedCount > 0)
    items.push(`<button class="bulk-del-item" data-action="status" data-status="skipped" data-days="0">Delete all skipped (${skippedCount})</button>`);
  if (noGoCount > 0)
    items.push(`<button class="bulk-del-item" data-action="fitlabel" data-label="no go">Delete all No Go (${noGoCount})</button>`);
  if (oldUnseenCount > 0)
    items.push(`<button class="bulk-del-item" data-action="status" data-status="unseen" data-days="30">Delete unseen &gt;30d (${oldUnseenCount})</button>`);

  menu.innerHTML = items.length
    ? items.join('')
    : '<span class="bulk-del-empty">Nothing to clean up</span>';

  menu.querySelectorAll('.bulk-del-item').forEach(btn => {
    btn.addEventListener('click', () => {
      const label = btn.textContent;
      showConfirm(`${label}? This cannot be undone.`, async () => {
        menu.classList.remove('open');
        try {
          const body = btn.dataset.action === 'fitlabel'
            ? { fit_label: btn.dataset.label }
            : { status: btn.dataset.status, older_than_days: parseInt(btn.dataset.days, 10) };
          const deleted = await bulkDeleteRequest(body);
          toastBulk(`Deleted ${deleted} jobs`);
          updateBulkDeleteMenu();
        } catch (e) {
          toastBulk(e.message, true);
        }
      });
    });
  });
}

export function initBulkDeleteDropdown() {
  const btn = document.getElementById('bulk-delete-btn');
  const menu = document.getElementById('bulk-delete-menu');
  if (!btn || !menu) return;

  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const isOpen = menu.classList.toggle('open');
    if (isOpen) updateBulkDeleteMenu();
  });

  document.addEventListener('click', () => menu.classList.remove('open'));
}