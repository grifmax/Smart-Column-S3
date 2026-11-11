// ============================================================================
// AI provider settings and OpenAI-compatible transport
// ============================================================================

const PROVIDER_KEY = 'sc3.ai.provider';
const DEVICE_CONFIG_URL = '/api/ai/config';

export const DEFAULT_PROVIDER = {
    baseUrl: '',
    apiKey: '',
    model: '',
    temperature: 0.2,
    maxTokens: 2000
};

export const PROVIDER_PRESETS = [
    { id: 'deepseek', label: 'DeepSeek', baseUrl: 'https://api.deepseek.com/v1', model: 'deepseek-chat' },
    { id: 'openai', label: 'OpenAI', baseUrl: 'https://api.openai.com/v1', model: 'gpt-4o-mini' },
    { id: 'openrouter', label: 'OpenRouter', baseUrl: 'https://openrouter.ai/api/v1', model: 'openai/gpt-4o-mini' },
    { id: 'local', label: 'Локальная (Ollama/LM Studio)', baseUrl: 'http://127.0.0.1:11434/v1', model: 'llama3.1' }
];

function normalizeBaseUrl(raw) {
    let url = String(raw || '').trim();
    if (!url) return '';
    url = url.replace(/\/+$/, '');
    return url;
}

export function loadProviderConfig() {
    try {
        const raw = localStorage.getItem(PROVIDER_KEY);
        if (!raw) return { ...DEFAULT_PROVIDER };
        const parsed = JSON.parse(raw);
        return {
            ...DEFAULT_PROVIDER,
            ...parsed,
            baseUrl: normalizeBaseUrl(parsed.baseUrl),
            temperature: Number.isFinite(Number(parsed.temperature)) ? Number(parsed.temperature) : DEFAULT_PROVIDER.temperature,
            maxTokens: Number.isFinite(Number(parsed.maxTokens)) ? Number(parsed.maxTokens) : DEFAULT_PROVIDER.maxTokens
        };
    } catch (error) {
        console.warn('AI: не удалось прочитать настройки провайдера', error);
        return { ...DEFAULT_PROVIDER };
    }
}

export function saveProviderConfig(config) {
    const normalized = {
        ...DEFAULT_PROVIDER,
        ...config,
        baseUrl: normalizeBaseUrl(config.baseUrl)
    };
    localStorage.setItem(PROVIDER_KEY, JSON.stringify(normalized));
    return normalized;
}

export function isProviderConfigured(config) {
    return Boolean(config && config.baseUrl && config.model);
}

// The device keeps the provider settings in NVS, so the phone, the tablet and
// the laptop all see the same assistant without retyping the key. The local
// copy stays as an offline cache for the first paint.
export async function loadProviderConfigFromDevice() {
    try {
        const response = await fetch(DEVICE_CONFIG_URL, { cache: 'no-store' });
        if (!response.ok) return null;
        const data = await response.json();
        if (!data?.configured) return null;
        const config = {
            baseUrl: normalizeBaseUrl(data.baseUrl || ''),
            apiKey: String(data.apiKey || ''),
            model: String(data.model || ''),
            temperature: Number.isFinite(Number(data.temperature)) ? Number(data.temperature) : DEFAULT_PROVIDER.temperature,
            maxTokens: Number.isFinite(Number(data.maxTokens)) ? Number(data.maxTokens) : DEFAULT_PROVIDER.maxTokens
        };
        if (!config.baseUrl || !config.model) return null;
        localStorage.setItem(PROVIDER_KEY, JSON.stringify(config));
        return config;
    } catch (_) {
        return null;
    }
}

export async function persistProviderConfig(config) {
    const saved = saveProviderConfig(config);
    try {
        const response = await fetch(DEVICE_CONFIG_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(saved)
        });
        return response.ok;
    } catch (_) {
        return false;
    }
}

function buildHeaders(config) {
    const headers = { 'Content-Type': 'application/json' };
    if (config.apiKey) {
        headers.Authorization = `Bearer ${config.apiKey}`;
    }
    return headers;
}

async function readError(response) {
    let detail;
    try {
        const text = await response.text();
        try {
            const parsed = JSON.parse(text);
            detail = parsed?.error?.message || parsed?.message || text;
        } catch (_) {
            detail = text;
        }
    } catch (_) {
        detail = '';
    }
    detail = String(detail || '').slice(0, 400);
    return `HTTP ${response.status}${detail ? `: ${detail}` : ''}`;
}

export async function fetchModels(config, { signal } = {}) {
    if (!config?.baseUrl) throw new Error('Не задан адрес провайдера');
    const response = await fetch(`${config.baseUrl}/models`, {
        method: 'GET',
        headers: buildHeaders(config),
        signal
    });
    if (!response.ok) {
        throw new Error(await readError(response));
    }
    const payload = await response.json();
    const list = Array.isArray(payload?.data) ? payload.data : Array.isArray(payload?.models) ? payload.models : [];
    return list
        .map((item) => (typeof item === 'string' ? item : item?.id || item?.name || ''))
        .filter(Boolean)
        .sort((a, b) => a.localeCompare(b));
}

// Non-streaming chat completion. Streaming was deliberately left out of the
// first version: tool calls and provider quirks are far easier to handle with
// a single JSON response, and the abort button covers the latency.
export async function chatCompletion(config, messages, { signal, tools } = {}) {
    if (!config?.baseUrl) throw new Error('Не задан адрес провайдера');
    if (!config?.model) throw new Error('Не выбрана модель');

    const body = {
        model: config.model,
        messages,
        temperature: config.temperature,
        max_tokens: config.maxTokens,
        stream: false
    };
    if (Array.isArray(tools) && tools.length) {
        body.tools = tools;
        body.tool_choice = 'auto';
    }

    const response = await fetch(`${config.baseUrl}/chat/completions`, {
        method: 'POST',
        headers: buildHeaders(config),
        body: JSON.stringify(body),
        signal
    });

    if (!response.ok) {
        throw new Error(await readError(response));
    }

    const payload = await response.json();
    const choice = payload?.choices?.[0];
    const message = choice?.message;
    if (!message) {
        throw new Error('Провайдер вернул пустой ответ');
    }

    return {
        message,
        usage: payload?.usage || null,
        finishReason: choice?.finish_reason || ''
    };
}

export function describeProviderError(error) {
    const text = String(error?.message || error || '');
    if (/Failed to fetch|NetworkError|Load failed/i.test(text)) {
        return 'Браузер не смог обратиться к провайдеру: проверьте адрес, интернет и CORS. Локальная модель должна быть доступна с телефона/ноутбука, а не только с localhost.';
    }
    if (/401|403/.test(text)) {
        return `Провайдер отклонил ключ (${text}). Проверьте API-ключ в настройках.`;
    }
    if (/429/.test(text)) {
        return `Провайдер ограничил частоту запросов (${text}). Подождите минуту.`;
    }
    return text;
}
