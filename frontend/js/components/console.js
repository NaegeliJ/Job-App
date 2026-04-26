import state from '../state.js';
import { bulkDeleteByStatus } from './actions.js';

const API_BASE = '/api';

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
    handler: () => `
Available commands:
  help                    Show this message
  clear                   Clear console output
  stats                   Show job statistics

  delete:status <status>  Soft-delete all skipped or unseen jobs
  delete:job <id>         Hard-delete a job completely
  fitcheck:clear <id>     Clear fit data for one job
  fitcheck:clear-all      Clear fit data for ALL jobs
  fitcheck:recheck <id>   Re-check fit for one job
  fitcheck:recheck-all     Clear all fit data + trigger batch recheck
`
  },

  clear: {
    desc: 'Clear console',
    handler: () => { clearConsole(); return null; }
  },

  stats: {
    desc: 'Show job statistics',
    handler: async () => {
      const res = await fetch(`${API_BASE}/jobs`);
      const jobs = await res.json();

      const total = jobs.length;
      const byStatus = {};
      const byLabel = {};
      let withFit = 0;

      jobs.forEach(job => {
        const status = job.user_status || 'unseen';
        byStatus[status] = (byStatus[status] || 0) + 1;

        const label = job.fit_label || 'none';
        byLabel[label] = (byLabel[label] || 0) + 1;

        if (job.fit_score !== undefined || job.fit_label) withFit++;
      });

      let output = `Total jobs: ${total}`;
      output += `\nWith fit assessment: ${withFit}`;

      output += '\n\nBy status:';
      Object.entries(byStatus).forEach(([k, v]) => {
        output += `\n  ${k}: ${v}`;
      });

      output += '\n\nBy label:';
      Object.entries(byLabel).forEach(([k, v]) => {
        output += `\n  ${k}: ${v}`;
      });

      return output;
    }
  },

  'delete:status': {
    desc: 'Soft-delete all jobs with given status (skipped or unseen)',
    usage: 'delete:status <skipped|unseen>',
    confirm: true,
    handler: async (args) => {
      const status = args[0];
      if (!status) throw new Error('Usage: delete:status <skipped|unseen>');
      if (!['skipped', 'unseen'].includes(status))
        throw new Error('Only skipped or unseen allowed');
      const deleted = await bulkDeleteByStatus(status, 0);
      return `Soft-deleted ${deleted} ${status} jobs`;
    }
  },

  'delete:job': {
    desc: 'Delete a job completely (hard delete)',
    usage: 'delete:job <id>',
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
      return `Deleted job ...${id.slice(-8)}`;
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

function logConsole(message, type = 'output') {
  const output = document.getElementById('console-output');
  if (!output) return;

  const line = document.createElement('div');
  line.className = `console-line ${type}`;
  line.textContent = message;
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

    logConsole(`$ ${commandLine}`, 'output');
    input.value = '';

    const parts = commandLine.split(' ');
    const cmdName = parts[0];
    const args = parts.slice(1);

    if (pendingConfirm) {
      if (cmdName.toLowerCase() === 'y' || cmdName.toLowerCase() === 'yes') {
        try {
          const result = await pendingConfirm.handler(pendingConfirm.args);
          if (result) logConsole(result, 'success');
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
      pendingConfirm = { handler: command.handler, args };
      logConsole('This action cannot be undone. Type "y" to confirm:', 'system');
      return;
    }

    try {
      const result = await command.handler(args);
      if (result) logConsole(result, 'success');
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