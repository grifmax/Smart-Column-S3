// ============================================================================
// AI panel: DOM wiring, provider settings, model picker
// ============================================================================

import {
    loadProviderConfig,
    saveProviderConfig,
    fetchModels,
    isProviderConfigured,
    describeProviderError,
    PROVIDER_PRESETS
} from './provider.js';
import { initChat, updateChatConfig, sendMessage, abortRequest, clearChat, isBusy } from './chat.js';

const MODELS_CACHE_KEY = 'sc3.ai.models';

let config = loadProviderConfig();
let elements = null;
let modelsRequest = null;

const QUICK_PROMPTS = [
    'Что сейчас происходит с установкой?',
    'Разбери последний прогон: где управление работало плохо?',
    'Что улучшить в профиле по последним прогонам?',
    'Почему скачет температура наверху колонны?'
];

function byId(id) {
    return document.getElementById(id);
}

function providerHostLabel() {
    if (!config.baseUrl) return 'провайдер не настроен';
    try {
        return new URL(config.baseUrl).host;
    } catch (_) {
        return config.baseUrl;
    }
}

function syncHeader() {
    const label = byId('ai-provider-label');
    if (label) {
        label.textContent = config.model ? `${config.model} · ${providerHostLabel()}` : 'модель не выбрана';
        label.classList.toggle('ai-provider-warning', !isProviderConfigured(config));
    }
    const select = byId('ai-model-select');
    if (select && config.model) {
        ensureOption(select, config.model);
        select.value = config.model;
    }
}

function ensureOption(select, value) {
    const exists = [...select.options].some((option) => option.value === value);
    if (!exists) {
        const option = document.createElement('option');
        option.value = value;
        option.textContent = value;
        select.appendChild(option);
    }
}

function fillModels(models) {
    const select = byId('ai-model-select');
    if (!select) return;
    const current = config.model;
    select.innerHTML = '';
    const seen = new Set();
    [current, ...models].filter(Boolean).forEach((model) => {
        if (seen.has(model)) return;
        seen.add(model);
        ensureOption(select, model);
    });
    if (current) select.value = current;
}

function loadCachedModels() {
    try {
        const raw = localStorage.getItem(MODELS_CACHE_KEY);
        return raw ? JSON.parse(raw) : [];
    } catch (_) {
        return [];
    }
}

function cacheModels(models) {
    try {
        localStorage.setItem(MODELS_CACHE_KEY, JSON.stringify(models.slice(0, 100)));
    } catch (_) {
        // Cache is optional.
    }
}

function readSettingsForm() {
    return {
        baseUrl: byId('ai-base-url')?.value.trim() || '',
        apiKey: byId('ai-api-key')?.value.trim() || '',
        model: byId('ai-model')?.value.trim() || '',
        temperature: Number(byId('ai-temperature')?.value || 0.2),
        maxTokens: Number(byId('ai-max-tokens')?.value || 2000)
    };
}

function writeSettingsForm(cfg) {
    if (byId('ai-base-url')) byId('ai-base-url').value = cfg.baseUrl || '';
    if (byId('ai-api-key')) byId('ai-api-key').value = cfg.apiKey || '';
    if (byId('ai-model')) byId('ai-model').value = cfg.model || '';
    if (byId('ai-temperature')) byId('ai-temperature').value = String(cfg.temperature ?? 0.2);
    if (byId('ai-max-tokens')) byId('ai-max-tokens').value = String(cfg.maxTokens ?? 2000);
}

function settingsHint(text, tone = '') {
    const hint = byId('ai-settings-hint');
    if (!hint) return;
    hint.textContent = text;
    hint.className = `ai-settings-hint ${tone ? `ai-settings-hint-${tone}` : ''}`;
}

export function openAiSettings() {
    writeSettingsForm(config);
    const modal = byId('ai-settings-modal');
    if (modal) modal.style.display = 'flex';
}

export function closeAiSettings() {
    const modal = byId('ai-settings-modal');
    if (modal) modal.style.display = 'none';
}

export function applyAiPreset(presetId) {
    const preset = PROVIDER_PRESETS.find((item) => item.id === presetId);
    if (!preset) return;
    if (byId('ai-base-url')) byId('ai-base-url').value = preset.baseUrl;
    if (byId('ai-model')) byId('ai-model').value = preset.model;
}

export async function loadAiModels() {
    if (modelsRequest) return;
    const form = readSettingsForm();
    if (!form.baseUrl) {
        settingsHint('Сначала укажите адрес провайдера', 'error');
        return;
    }

    const button = byId('ai-load-models');
    if (button) {
        button.disabled = true;
        button.textContent = 'Загрузка…';
    }
    settingsHint('Запрашиваю список моделей…');

    modelsRequest = fetchModels(form);
    try {
        const models = await modelsRequest;
        if (!models.length) {
            settingsHint('Провайдер вернул пустой список. Введите id модели вручную.', 'warning');
        } else {
            cacheModels(models);
            const datalist = byId('ai-models-list');
            if (datalist) {
                datalist.innerHTML = '';
                models.forEach((model) => {
                    const option = document.createElement('option');
                    option.value = model;
                    datalist.appendChild(option);
                });
            }
            fillModels(models);
            settingsHint(`Доступно моделей: ${models.length}`, 'ok');
        }
    } catch (error) {
        settingsHint(`Не удалось получить модели: ${describeProviderError(error)}`, 'error');
    } finally {
        modelsRequest = null;
        if (button) {
            button.disabled = false;
            button.textContent = 'Загрузить модели';
        }
    }
}

export function saveAiSettings() {
    const next = readSettingsForm();
    if (!next.baseUrl) {
        settingsHint('Адрес провайдера обязателен', 'error');
        return;
    }
    if (!/^https?:\/\//i.test(next.baseUrl)) {
        settingsHint('Адрес должен начинаться с http:// или https://', 'error');
        return;
    }
    if (!next.model) {
        settingsHint('Выберите или введите модель', 'error');
        return;
    }

    config = saveProviderConfig(next);
    updateChatConfig(config);
    syncHeader();
    fillModels(loadCachedModels());
    closeAiSettings();
    settingsHint('Настройки сохранены', 'ok');
}

async function handleSend() {
    if (isBusy()) return;
    if (!isProviderConfigured(config)) {
        openAiSettings();
        settingsHint('Укажите адрес провайдера, ключ и модель — без этого чат не работает', 'error');
        return;
    }
    const input = elements?.input;
    if (!input) return;
    const text = input.value.trim();
    if (!text) return;
    input.value = '';
    input.style.height = 'auto';
    sendMessage(text);
}

function fitPanelHeight() {
    const section = byId('ai');
    const area = document.querySelector('.content-area');
    const panel = section?.querySelector('.ai-panel');
    if (!section || !area || !panel || !section.classList.contains('active')) return;
    const bottom = area.getBoundingClientRect().bottom;
    const top = section.getBoundingClientRect().top;
    // The panel must fit the visible content area, not the viewport: on a
    // phone the toolbar, KPI strip and status line take a large and variable
    // share, and their height differs between tabs.
    panel.style.height = `${Math.max(300, Math.floor(bottom - top - 12))}px`;
}

function scheduleFitPanelHeight() {
    fitPanelHeight();
    requestAnimationFrame(fitPanelHeight);
    setTimeout(fitPanelHeight, 120);
}

function autoGrow(textarea) {
    textarea.style.height = 'auto';
    textarea.style.height = `${Math.min(180, textarea.scrollHeight)}px`;
}

function buildQuickPrompts() {
    const container = byId('ai-quick-prompts');
    if (!container) return;
    container.innerHTML = '';
    QUICK_PROMPTS.forEach((prompt) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'ai-quick-prompt';
        button.textContent = prompt;
        button.addEventListener('click', () => sendMessage(prompt));
        container.appendChild(button);
    });
}

export function initAiPanel() {
    const section = byId('ai');
    if (!section) return;

    elements = {
        messages: byId('ai-messages'),
        input: byId('ai-input'),
        send: byId('ai-send'),
        stop: byId('ai-stop'),
        status: byId('ai-status'),
        typing: byId('ai-typing')
    };
    if (!elements.messages || !elements.input || !elements.send) return;

    initChat(elements, config);
    syncHeader();
    fillModels(loadCachedModels());
    buildQuickPrompts();

    elements.send.addEventListener('click', handleSend);
    elements.stop?.addEventListener('click', abortRequest);
    elements.input.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && !event.shiftKey) {
            event.preventDefault();
            handleSend();
        }
    });
    elements.input.addEventListener('input', () => autoGrow(elements.input));

    byId('ai-clear')?.addEventListener('click', clearChat);
    byId('ai-settings-open')?.addEventListener('click', openAiSettings);
    byId('ai-save-settings')?.addEventListener('click', saveAiSettings);
    byId('ai-close-settings')?.addEventListener('click', closeAiSettings);
    byId('ai-load-models')?.addEventListener('click', loadAiModels);
    byId('ai-settings-modal')?.addEventListener('click', (event) => {
        if (event.target?.id === 'ai-settings-modal') closeAiSettings();
    });
    document.querySelectorAll('[data-ai-preset]').forEach((button) => {
        button.addEventListener('click', () => applyAiPreset(button.getAttribute('data-ai-preset')));
    });

    byId('ai-model-select')?.addEventListener('change', (event) => {
        config = saveProviderConfig({ ...config, model: event.target.value });
        updateChatConfig(config);
        syncHeader();
    });

    document.addEventListener('app-tab-changed', (event) => {
        if (event.detail?.tabId === 'ai') {
            const area = document.querySelector('.content-area');
            if (area) area.scrollTop = 0;
            scheduleFitPanelHeight();
            elements.messages.scrollTop = elements.messages.scrollHeight;
        }
    });

    window.addEventListener('resize', fitPanelHeight);
    const contentArea = document.querySelector('.content-area');
    if (contentArea && typeof ResizeObserver === 'function') {
        new ResizeObserver(fitPanelHeight).observe(contentArea);
    }
    scheduleFitPanelHeight();

    // Inline handlers used from the markup keep working even if a module
    // listener was not attached (e.g. markup rendered later).
    window.openAiSettings = openAiSettings;
    window.closeAiSettings = closeAiSettings;
    window.saveAiSettings = saveAiSettings;
    window.loadAiModels = loadAiModels;
    window.applyAiPreset = applyAiPreset;
}
