import state from '../state.js';
import { bulkDeleteByStatus, bulkDeleteByFitLabel } from './actions.js';
import { renderList } from './job-list.js';
import { updateStats, updateBulkDeleteMenu } from './header.js';

const API_BASE = '/api';

async function refreshState() {
  state.allJobs = await fetch(`${API_BASE}/jobs`).then(r => r.json());
  renderList();
  updateStats();
  updateBulkDeleteMenu();
}

function resolveJobId(input) {
  if (!input) throw new Error('Usage: command <id>');

  const exact = state.allJobs.find(j => j.job_id === input);
  if (exact) return input;

  const matches = state.allJobs.filter(j => j.job_id.endsWith(input));
  if (matches.length === 1) return matches[0].job_id;
  if (matches.length > 1) {
    throw new Error(`Ambiguous ID "${input}" matches ${matches.length} jobs. Use more characters.`);
  }

  throw new Error(`Job not found: ${input}`);
}

const COMMANDS = {
  help: {
    desc: 'Show available commands',
    pure: true,
    handler: () => {
      const cmd = (name, arg, desc) => {
        const argHtml = arg ? `&lt;${arg}&gt;` : '';
        logConsole(
          `  <span class="h-cmd">${name}</span><span class="h-arg">${argHtml}</span><span class="h-desc">${desc}</span>`,
          'output', true
        );
      };
      const sec = (title, note = '') => {
        const noteHtml = note ? `<span class="h-note">${note}</span>` : '';
        logConsole(`<span class="h-section">${title}</span>${noteHtml}`, 'output', true);
      };
      const gap = () => logConsole('', 'output');

      sec('General');
      cmd('clear',   '',        'clear output');
      cmd('stats',   '',        'job statistics');
      gap();
      sec('Operations');
      cmd('scrape',   '',       'full scrape · jobs + details');
      cmd('fitcheck', '',       'batch fit-check unassessed');
      gap();
      sec('Soft Delete', '· hidden, reversible');
      cmd('delete:status', 'status',  'all with user status');
      cmd('delete:label',  'label',  'all with fit label');
      cmd('delete:job',    'id',     'one job');
      cmd('restore:all',   '',       'restore all deleted to unseen');
      gap();
      sec('Purge', '· permanent');
      cmd('purge:job',   'id',    'one job');
      cmd('purge:label', 'label', 'all with fit label');
      gap();
      sec('Fit-Check');
      cmd('fitcheck:clear',       'id', 'clear fit data for one job');
      cmd('fitcheck:clear-all',   '',   'clear all fit data');
      cmd('fitcheck:recheck',     'id', 'recheck one job');
      cmd('fitcheck:recheck-all', '',   'clear all + batch recheck');
      gap();
      return null;
    }
  },

  clear: {
    desc: 'Clear console',
    pure: true,
    handler: () => { clearConsole(); return null; }
  },

  stats: {
    desc: 'Show job statistics',
    pure: true,
    handler: async () => {
      const res = await fetch(`${API_BASE}/jobs`);
      const jobs = await res.json();

      const deleted = jobs.filter(j => j.user_status === 'deleted').length;
      const active = jobs.filter(j => j.user_status !== 'deleted');
      const byStatus = {};
      const byLabel = {};
      let withFit = 0;

      active.forEach(job => {
        const status = job.user_status || 'unseen';
        byStatus[status] = (byStatus[status] || 0) + 1;

        const label = job.fit_label || 'none';
        byLabel[label] = (byLabel[label] || 0) + 1;

        if (job.fit_score !== undefined || job.fit_label) withFit++;
      });

      let output = `Total active: ${active.length}  (${deleted} soft-deleted)`;
      output += `\nWith fit assessment: ${withFit}`;

      output += '\n\nBy status:';
      Object.entries(byStatus).forEach(([k, v]) => {
        output += `\n  ${k}: ${v}`;
      });

      output += '\n\nBy fit label:';
      Object.entries(byLabel).forEach(([k, v]) => {
        output += `\n  ${k}: ${v}`;
      });

      return output;
    }
  },

  scrape: {
    desc: 'Trigger full scrape (jobs + details)',
    handler: async () => {
      logConsole('Scraping jobs...', 'system');
      const r1 = await fetch(`${API_BASE}/scrape/jobs`, { method: 'POST' });
      if (!r1.ok) throw new Error(`Scrape jobs failed: HTTP ${r1.status}`);
      const d1 = await r1.json();
      logConsole(`Jobs scraped: ${d1.count ?? '?'} processed. Fetching details...`, 'system');

      const r2 = await fetch(`${API_BASE}/scrape/details`, { method: 'POST' });
      if (!r2.ok) throw new Error(`Scrape details failed: HTTP ${r2.status}`);
      const d2 = await r2.json();
      return `Scrape complete. Jobs: ${d1.count ?? '?'} processed, details: ${d2.updated ?? '?'} updated, ${d2.failed ?? 0} failed`;
    }
  },

  fitcheck: {
    desc: 'Run batch fit-check on unassessed jobs',
    handler: async () => {
      logConsole('Running fit-check...', 'system');
      const res = await fetch(`${API_BASE}/fitcheck`, { method: 'POST' });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      const data = await res.json();
      return `Fit-check complete: ${data.checked} checked, ${data.failed} failed`;
    }
  },

  'delete:status': {
    desc: 'Soft-delete all jobs with given status',
    usage: 'delete:status <status>',
    confirm: true,
    handler: async (args) => {
      const status = args[0];
      if (!status) throw new Error('Usage: delete:status <status>');
      const deleted = await bulkDeleteByStatus(status, 0);
      return `Soft-deleted ${deleted} ${status} jobs`;
    }
  },

  'delete:label': {
    desc: 'Soft-delete all jobs with given fit label',
    usage: 'delete:label <label>',
    confirm: true,
    handler: async (args) => {
      const label = args.join(' ');
      if (!label) throw new Error('Usage: delete:label <label>');
      const deleted = await bulkDeleteByFitLabel(label);
      return `Soft-deleted ${deleted} "${label}" jobs`;
    }
  },

  'delete:job': {
    desc: 'Soft-delete one job',
    usage: 'delete:job <id>',
    confirm: true,
    handler: async (args) => {
      const id = resolveJobId(args[0]);
      logConsole(`Resolved: ...${id.slice(-8)}`, 'system');
      const res = await fetch(`${API_BASE}/jobs/${encodeURIComponent(id)}/soft-delete`, {
        method: 'POST'
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      return `Soft-deleted job ...${id.slice(-8)}`;
    }
  },

  'restore:all': {
    desc: 'Restore all soft-deleted jobs to unseen',
    confirm: true,
    handler: async () => {
      const res = await fetch(`${API_BASE}/jobs/restore-all`, { method: 'POST' });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      const data = await res.json();
      return `Restored ${data.restored} jobs to unseen`;
    }
  },

  'purge:job': {
    desc: 'Hard-delete one job permanently',
    usage: 'purge:job <id>',
    confirm: true,
    handler: async (args) => {
      const id = resolveJobId(args[0]);
      logConsole(`Resolved: ...${id.slice(-8)}`, 'system');
      const res = await fetch(`${API_BASE}/admin/jobs/${encodeURIComponent(id)}`, {
        method: 'DELETE'
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      return `Purged job ...${id.slice(-8)}`;
    }
  },

  'purge:label': {
    desc: 'Hard-delete all jobs with given fit label',
    usage: 'purge:label <label>',
    confirm: true,
    handler: async (args) => {
      const label = args.join(' ');
      if (!label) throw new Error('Usage: purge:label <label>');
      const res = await fetch(`${API_BASE}/admin/jobs/bulk`, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ fit_label: label })
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      const data = await res.json();
      return `Purged ${data.deleted} "${label}" jobs`;
    }
  },

  'fitcheck:clear': {
    desc: 'Clear fit data for one job',
    usage: 'fitcheck:clear <id>',
    confirm: true,
    handler: async (args) => {
      const id = resolveJobId(args[0]);
      logConsole(`Resolved: ...${id.slice(-8)}`, 'system');

      const res = await fetch(`${API_BASE}/admin/fitcheck/clear/${encodeURIComponent(id)}`, {
        method: 'POST'
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      return `Cleared fit data for ...${id.slice(-8)}`;
    }
  },

  'fitcheck:clear-all': {
    desc: 'Clear fit data for ALL jobs',
    confirm: true,
    handler: async () => {
      const res = await fetch(`${API_BASE}/admin/fitcheck/clear`, {
        method: 'POST'
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      return 'Cleared fit data for all jobs. Run fitcheck to re-assess.';
    }
  },

  'fitcheck:recheck': {
    desc: 'Re-check fit for one job',
    usage: 'fitcheck:recheck <id>',
    confirm: true,
    handler: async (args) => {
      const id = resolveJobId(args[0]);
      logConsole(`Resolved: ...${id.slice(-8)}`, 'system');
      logConsole('Clearing fit data and triggering recheck...', 'system');

      const res = await fetch(`${API_BASE}/admin/fitcheck/recheck/${encodeURIComponent(id)}`, {
        method: 'POST'
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      const data = await res.json();
      return `Rechecked ...${id.slice(-8)}: fit_score=${data.fit_score}, fit_label=${data.fit_label}`;
    }
  },

  'fitcheck:recheck-all': {
    desc: 'Clear all fit data + trigger batch recheck',
    confirm: true,
    handler: async () => {
      logConsole('Clearing all fit data...', 'system');

      const clearRes = await fetch(`${API_BASE}/admin/fitcheck/recheck`, {
        method: 'POST'
      });
      if (!clearRes.ok) {
        const data = await clearRes.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${clearRes.status}`);
      }

      logConsole('All fit data cleared. Starting batch fitcheck...', 'system');

      const fitcheckRes = await fetch(`${API_BASE}/fitcheck`, {
        method: 'POST'
      });
      if (!fitcheckRes.ok) {
        const data = await fitcheckRes.json().catch(() => ({}));
        throw new Error(data.error || `HTTP ${fitcheckRes.status}`);
      }
      const fitcheckData = await fitcheckRes.json();
      return `Batch recheck complete: ${fitcheckData.checked} checked, ${fitcheckData.failed} failed`;
    }
  }
};

let consoleVisible = false;
let consoleHistory = [];
let historyIndex = -1;
let pendingConfirm = null;

function createConsole() {
  if (document.getElementById('dev-console')) return;

  const overlay = document.createElement('div');
  overlay.id = 'dev-console';
  overlay.className = 'console-overlay';
  overlay.innerHTML = `
    <div class="console-header">
      <span class="console-title">Developer Console</span>
      <button class="console-close" aria-label="Close console">×</button>
    </div>
    <div class="console-output" id="console-output"></div>
    <div class="console-input-row">
      <span class="console-prompt">$</span>
      <input type="text" class="console-input" id="console-input" placeholder="Type command..." autocomplete="off" spellcheck="false">
    </div>
    <div class="console-hint">
      <strong>Ctrl+\\</strong> to toggle • Type <strong>help</strong> for commands
    </div>
  `;

  document.body.appendChild(overlay);

  overlay.querySelector('.console-close').onclick = hideConsole;

  const input = document.getElementById('console-input');
  input.addEventListener('keydown', handleConsoleInput);

  logConsole('Developer console ready. Type "help" for commands.', 'system');
}

function showConsole() {
  if (!document.getElementById('dev-console')) {
    createConsole();
  }
  document.getElementById('dev-console').classList.add('show');
  document.getElementById('console-input').focus();
  consoleVisible = true;
}

function hideConsole() {
  const el = document.getElementById('dev-console');
  if (el) el.classList.remove('show');
  consoleVisible = false;
  pendingConfirm = null;
}

function toggleConsole() {
  if (consoleVisible) hideConsole();
  else showConsole();
}

function logConsole(message, type = 'output', html = false) {
  const output = document.getElementById('console-output');
  if (!output) return;

  const line = document.createElement('div');
  line.className = `console-line ${type}`;
  if (html) line.innerHTML = message;
  else line.textContent = message;
  output.appendChild(line);
  output.scrollTop = output.scrollHeight;
}

function clearConsole() {
  const output = document.getElementById('console-output');
  if (output) output.innerHTML = '';
}

async function handleConsoleInput(e) {
  const input = document.getElementById('console-input');

  if (e.key === 'Enter') {
    const commandLine = input.value.trim();
    if (!commandLine) return;

    consoleHistory.push(commandLine);
    historyIndex = consoleHistory.length;

    logConsole(`$ ${commandLine}`, 'echo');
    input.value = '';

    const parts = commandLine.split(' ');
    const cmdName = parts[0];
    const args = parts.slice(1);

    if (pendingConfirm) {
      if (cmdName.toLowerCase() === 'y' || cmdName.toLowerCase() === 'yes') {
        try {
          const result = await pendingConfirm.handler(pendingConfirm.args);
          if (result) logConsole(result, 'success');
          if (!pendingConfirm?.pure) await refreshState();
        } catch (err) {
          logConsole(err.message, 'error');
        }
      } else {
        logConsole('Cancelled.', 'system');
      }
      pendingConfirm = null;
      return;
    }

    const command = COMMANDS[cmdName];
    if (!command) {
      logConsole(`Unknown command: ${cmdName}. Type "help" for available commands.`, 'error');
      return;
    }

    if (command.confirm) {
      pendingConfirm = { handler: command.handler, args, pure: command.pure };
      logConsole('This action cannot be undone. Type "y" to confirm:', 'system');
      return;
    }

    try {
      const result = await command.handler(args);
      if (result) logConsole(result, 'success');
      if (!command.pure) await refreshState();
    } catch (err) {
      logConsole(err.message, 'error');
    }

  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    if (historyIndex > 0) {
      historyIndex--;
      input.value = consoleHistory[historyIndex] || '';
    }
  } else if (e.key === 'ArrowDown') {
    e.preventDefault();
    if (historyIndex < consoleHistory.length - 1) {
      historyIndex++;
      input.value = consoleHistory[historyIndex] || '';
    } else {
      historyIndex = consoleHistory.length;
      input.value = '';
    }
  } else if (e.key === 'Escape') {
    hideConsole();
  }
}

export function initConsole() {
  document.addEventListener('keydown', (e) => {
    if (e.ctrlKey && e.code === 'Backslash') {
      e.preventDefault();
      toggleConsole();
    }
  });
}

export { toggleConsole };