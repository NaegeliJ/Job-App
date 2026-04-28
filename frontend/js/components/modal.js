import state from '../state.js';
import { CONFIG_URL, VERSION_URL } from '../api.js';
import { showToast } from './actions.js';
import { escapeHtml } from '../utils/formatting.js';

const PROVIDERS = {
  ollama_local: { name: 'Ollama (Local)',  endpoint: 'http://localhost:11434/api/chat',                      models: ['llama3.2:latest', 'llama3.2:3b', 'llama3.1:8b'],                                                      needsKey: false },
  ollama_cloud: { name: 'Ollama Cloud',    endpoint: 'https://ollama.com/v1/chat/completions',               models: ['gemma4:31b-cloud', 'deepseek-v4-flash'],                                                      needsKey: true  },
  openrouter:   { name: 'OpenRouter',      endpoint: 'https://openrouter.ai/api/v1/chat/completions',        models: ['mistralai/mistral-nemo', 'meta-llama/llama-3.1-70b-instruct', 'qwen/qwen-2.5-72b-instruct'],                                           needsKey: true  },
  deepinfra:    { name: 'DeepInfra',       endpoint: 'https://api.deepinfra.com/v1/openai/chat/completions', models: ['mistralai/Mistral-Nemo-Instruct-2407', 'meta-llama/Meta-Llama-3.1-70B-Instruct', 'Qwen/Qwen2.5-72B-Instruct'],                    needsKey: true  },
  mistral:      { name: 'Mistral',         endpoint: 'https://api.mistral.ai/v1/chat/completions',           models: ['mistral-small-latest'],                                                                       needsKey: true  },
  custom:       { name: 'Custom',          endpoint: '',                                                      models: [],                                                                                             needsKey: true  }
};

let rawConfig = null;
let rawAiConfig = null;

// ============================================================================
// Open/Close
// ============================================================================

export async function openSettings() {
  const overlay = document.getElementById('settings-overlay');
  const body = document.getElementById('settings-body');

  if (!overlay || !body) return;

  overlay.classList.add('open');
  body.innerHTML = renderLoadingState();

  try {
    const [cfgRes, aiRes] = await Promise.all([
      fetch(CONFIG_URL),
      fetch('/api/config/ai')
    ]);
    rawConfig = await cfgRes.json();
    rawAiConfig = await aiRes.json();
    body.innerHTML = renderConfigForm(rawConfig, rawAiConfig);
    setupProviderHandlers();
  } catch (error) {
    body.innerHTML = renderErrorState('Failed to load config');
  }

  fetch(VERSION_URL)
    .then(r => r.json())
    .then(ver => {
      const verEl = document.getElementById('app-version');
      if (verEl) verEl.textContent = `v${ver.version}`;
    })
    .catch(() => {});
}

export function closeSettings() {
  const overlay = document.getElementById('settings-overlay');
  if (overlay) overlay.classList.remove('open');
}

export function closeSettingsOnBg(event) {
  const overlay = document.getElementById('settings-overlay');
  if (!overlay) return;
  if (event.target === overlay && state._modalMousedownTarget === overlay) {
    closeSettings();
  }
}

// ============================================================================
// Provider Handler
// ============================================================================

function setupProviderHandlers() {
  const select = document.getElementById('cfg-ai-provider');
  if (select) select.addEventListener('change', () => updateProviderUI(select.value));

  const body = document.getElementById('settings-body');
  if (body) body.addEventListener('click', e => {
    const chip = e.target.closest('.model-chip');
    if (!chip) return;
    const modelEl = document.getElementById('cfg-ai-model');
    if (modelEl) modelEl.value = chip.dataset.model;
    chip.closest('#cfg-ai-model-chips')?.querySelectorAll('.model-chip')
      .forEach(c => c.classList.remove('active'));
    chip.classList.add('active');
  });
}

function updateProviderUI(providerKey) {
  const p = PROVIDERS[providerKey];
  if (!p) return;
  const endpointEl = document.getElementById('cfg-ai-endpoint');
  const modelEl    = document.getElementById('cfg-ai-model');
  const keyEl      = document.getElementById('cfg-ai-key');
  const chipsEl    = document.getElementById('cfg-ai-model-chips');
  const keyNote    = document.getElementById('cfg-ai-key-note');

  if (endpointEl && p.endpoint) endpointEl.value = p.endpoint;
  if (modelEl && p.models.length) modelEl.value = p.models[0];

  if (chipsEl) {
    chipsEl.innerHTML = p.models.map(m =>
      `<span class="model-chip" data-model="${escapeHtml(m)}">${escapeHtml(m)}</span>`
    ).join('');
  }

  if (keyEl) {
    keyEl.disabled = !p.needsKey;
    keyEl.placeholder = p.needsKey ? 'Enter API Key' : 'No key required for local Ollama';
    if (!p.needsKey) keyEl.value = '';
  }
  if (keyNote) {
    keyNote.textContent = p.needsKey ? 'Stored in config/api_keys.json. Leave blank to keep current.' : 'Ollama local does not need an API key.';
  }
}

// ============================================================================
// Render Helpers
// ============================================================================

function renderLoadingState() {
  return `
    <div class="ldw">
      <span class="ld">●</span>
      <span class="ld">●</span>
      <span class="ld">●</span>
    </div>`;
}

function renderErrorState(message) {
  return `<div style="color:var(--red);font-size:12px">${message}</div>`;
}

function renderSection(title, content) {
  return `
    <div class="cfg-section">
      <div class="cfg-section-title">${title}</div>
      ${content}
    </div>`;
}

function renderField(label, inputHtml) {
  return `
    <div class="cfg-field">
      <div class="cfg-label">${label}</div>
      ${inputHtml}
    </div>`;
}

function renderTextarea(id, value, options = {}) {
  const { minHeight = '60px', placeholder = '' } = options;
  return `<textarea class="cfg-textarea" id="${id}" placeholder="${escapeHtml(placeholder)}" style="min-height:${minHeight}">${escapeHtml(String(value))}</textarea>`;
}

function renderInput(id, value, type = 'number') {
  return `<input class="cfg-input" id="${id}" type="${type}" value="${escapeHtml(String(value))}">`;
}

function renderGrid(fields) {
  return `<div class="cfg-grid">${fields.join('')}</div>`;
}

// ============================================================================
// Form Rendering
// ============================================================================

function renderAiSection(aiConfig) {
  const currentProvider = aiConfig.provider || 'ollama_local';
  const p = PROVIDERS[currentProvider] || PROVIDERS.custom;

  const providerOptions = Object.entries(PROVIDERS)
    .map(([k, v]) => `<option value="${k}"${k === currentProvider ? ' selected' : ''}>${escapeHtml(v.name)}</option>`)
    .join('');

  const chips = p.models.map(m =>
    `<span class="model-chip${(aiConfig.model === m) ? ' active' : ''}" data-model="${escapeHtml(m)}">${escapeHtml(m)}</span>`
  ).join('');

  const fields = [
    renderField('Provider', `<select class="cfg-input" id="cfg-ai-provider">${providerOptions}</select>`),
    renderField('Endpoint URL', `<input class="cfg-input" id="cfg-ai-endpoint" type="text" value="${escapeHtml(aiConfig.endpoint || '')}">`),
    renderField('Model',
      `<input class="cfg-input" id="cfg-ai-model" type="text" value="${escapeHtml(aiConfig.model || '')}">` +
      `<div id="cfg-ai-model-chips" style="display:flex;flex-wrap:wrap;gap:4px;margin-top:6px">${chips}</div>`
    ),
    renderField('API Key',
      `<input class="cfg-input" id="cfg-ai-key" type="password" ${!p.needsKey ? 'disabled' : ''} placeholder="${p.needsKey ? 'Enter New API Key' : 'No key required for local Ollama'}">` +
      `<div id="cfg-ai-key-note" style="font-size:11px;color:var(--text3);margin-top:4px">${p.needsKey ? 'Stored in config/api_keys.json. Leave blank to keep current.' : 'Ollama local does not need an API key.'}</div>`
    )
  ];

  return renderSection('AI Provider', renderGrid(fields));
}

function renderScrapeSection(config) {
  const scrape = config.scrape || {};
  const fields = [
    renderField('Search Queries (one per line)',
      renderTextarea('cfg-queries', (scrape.queries || []).join('\n'), { minHeight: '100px' })),
    renderField('Rows per Query', renderInput('cfg-rows', scrape.rows ?? 50))
  ];
  return renderSection('Scraping', renderGrid(fields));
}

function renderFitcheckSection(config) {
  const fc = config.fitcheck || {};
  const fields = [
    renderField('Job Limit',    renderInput('cfg-fc-limit',       fc.limit       ?? 50)),
    renderField('Max Tokens',   renderInput('cfg-fc-max-tokens',  fc.max_tokens  ?? 4000)),
    renderField('Temperature',  renderInput('cfg-fc-temperature', fc.temperature ?? 1.0, 'number')),
    renderField('Top P',        renderInput('cfg-fc-top-p',       fc.top_p       ?? 0.95, 'number')),
    renderField('Top K',        renderInput('cfg-fc-top-k',       fc.top_k       ?? 64))
  ];
  return renderSection('Fit-Check (Advanced)', renderGrid(fields));
}

export function renderConfigForm(config, aiConfig) {
  return [
    renderAiSection(aiConfig || {}),
    renderScrapeSection(config),
    renderFitcheckSection(config)
  ].join('');
}

// ============================================================================
// Save Settings
// ============================================================================

function getIntValue(id, defaultValue = 0) {
  const element = document.getElementById(id);
  if (!element) return defaultValue;
  const value = parseInt(element.value);
  return isNaN(value) ? defaultValue : value;
}

function getFloatValue(id, defaultValue = 0) {
  const element = document.getElementById(id);
  if (!element) return defaultValue;
  const value = parseFloat(element.value);
  return isNaN(value) ? defaultValue : value;
}

function getStringValue(id, defaultValue = '') {
  const element = document.getElementById(id);
  return element ? element.value.trim() : defaultValue;
}

function getTextareaLines(id) {
  const element = document.getElementById(id);
  if (!element) return [];
  return element.value.split('\n').map(s => s.trim()).filter(Boolean);
}

export async function saveSettings() {
  if (!rawConfig) return;

  try {
    const provider = getStringValue('cfg-ai-provider');
    const endpoint = getStringValue('cfg-ai-endpoint');
    const model    = getStringValue('cfg-ai-model');
    const apiKey   = getStringValue('cfg-ai-key');

    // Save AI provider config
    const aiRes = await fetch('/api/config/ai', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ provider, endpoint, model, api_key: apiKey })
    });
    const aiData = await aiRes.json();
    if (!aiRes.ok) {
      showToast('AI config error: ' + (aiData.error || 'unknown'), true);
      return;
    }

    // Save main config (scrape + advanced fitcheck params)
    const updated = JSON.parse(JSON.stringify(rawConfig));
    updated.scrape = {
      queries: getTextareaLines('cfg-queries'),
      rows: getIntValue('cfg-rows', 50)
    };
    updated.fitcheck = {
      ...(updated.fitcheck || {}),
      provider,
      endpoint,
      model,
      limit:       getIntValue('cfg-fc-limit', 50),
      max_tokens:  getIntValue('cfg-fc-max-tokens', 4000),
      temperature: getFloatValue('cfg-fc-temperature', 1.0),
      top_p:       getFloatValue('cfg-fc-top-p', 0.95),
      top_k:       getIntValue('cfg-fc-top-k', 64)
    };

    const cfgRes = await fetch(CONFIG_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(updated)
    });
    const cfgData = await cfgRes.json();

    if (cfgRes.ok) {
      showToast('Config saved & reloaded');
      closeSettings();
    } else {
      showToast('Error: ' + (cfgData.error || 'unknown'), true);
    }
  } catch (error) {
    showToast('Save failed', true);
  }
}
