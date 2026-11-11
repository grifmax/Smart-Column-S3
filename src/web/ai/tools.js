// ============================================================================
// Read-only tools exposed to the model
// ============================================================================
//
// Everything here maps to an existing GET endpoint. No POST/PUT route is in
// this file on purpose: the assistant must not be able to start a process,
// move a valve or change a setpoint.

import { buildStatePack, buildRunPack, buildLogDigest, compareRunPacks } from './context.js';

const MAX_TOOL_RESULT_CHARS = 8000;

export const TOOL_DEFINITIONS = [
    {
        type: 'function',
        function: {
            name: 'get_status',
            description: 'Текущее состояние установки: режим, фаза, температуры, мощность, насос, давление, guidance и ключевые indicators.',
            parameters: { type: 'object', properties: {}, additionalProperties: false }
        }
    },
    {
        type: 'function',
        function: {
            name: 'get_health',
            description: 'Здоровье подсистем: датчики, Wi-Fi, heap, uptime, счётчики control-цикла и ошибки.',
            parameters: { type: 'object', properties: {}, additionalProperties: false }
        }
    },
    {
        type: 'function',
        function: {
            name: 'list_processes',
            description: 'Список сохранённых прогонов с типом, длительностью, статусом и собранным объёмом.',
            parameters: {
                type: 'object',
                properties: {
                    limit: { type: 'integer', description: 'Сколько последних прогонов вернуть (1-20)', minimum: 1, maximum: 20 }
                },
                additionalProperties: false
            }
        }
    },
    {
        type: 'function',
        function: {
            name: 'get_process',
            description: 'Подробности одного прогона: параметры, фазы с агрегатами управления, метрики, advisorSnapshot, события и прореженный ряд.',
            parameters: {
                type: 'object',
                properties: {
                    id: { type: 'string', description: 'ID прогона из list_processes' }
                },
                required: ['id'],
                additionalProperties: false
            }
        }
    },
    {
        type: 'function',
        function: {
            name: 'compare_runs',
            description: 'Сравнить два прогона: дельта длительности, фаз и агрегатов управления.',
            parameters: {
                type: 'object',
                properties: {
                    baseId: { type: 'string', description: 'Базовый (более старый) прогон' },
                    compareId: { type: 'string', description: 'Прогон для сравнения' }
                },
                required: ['baseId', 'compareId'],
                additionalProperties: false
            }
        }
    },
    {
        type: 'function',
        function: {
            name: 'get_logs',
            description: 'Последние события журнала: счётчики по уровням и читаемый хвост с reasonCode.',
            parameters: {
                type: 'object',
                properties: {
                    limit: { type: 'integer', description: 'Сколько последних событий вернуть (1-100)', minimum: 1, maximum: 100 }
                },
                additionalProperties: false
            }
        }
    },
    {
        type: 'function',
        function: {
            name: 'get_live_chart',
            description: 'Живые графики: минутный ряд (последний час) и 10-минутные агрегаты (до 12 часов).',
            parameters: { type: 'object', properties: {}, additionalProperties: false }
        }
    }
];

async function fetchJson(path) {
    const response = await fetch(path, { headers: { Accept: 'application/json' }, cache: 'no-store' });
    if (!response.ok) {
        throw new Error(`GET ${path} → HTTP ${response.status}`);
    }
    return response.json();
}

function packChart(payload) {
    const compactPoint = (point) => {
        const out = { ms: point.ms };
        ['t_cube', 't_column_top', 't_column_bottom', 't_reflux', 'p_cube'].forEach((key) => {
            if (Number.isFinite(Number(point[key]))) out[key] = Math.round(Number(point[key]) * 10) / 10;
        });
        if (Number.isFinite(Number(point.power))) out.power = Math.round(Number(point.power));
        if (Number.isFinite(Number(point.pump_speed))) out.pump = Math.round(Number(point.pump_speed));
        return out;
    };
    return {
        minuteIntervalSec: payload?.minuteIntervalSec,
        aggregateIntervalSec: payload?.aggregateIntervalSec,
        minute: (payload?.minute || []).map(compactPoint),
        aggregate: (payload?.aggregate || []).slice(-48).map(compactPoint)
    };
}

function packHealth(payload) {
    const system = payload?.system || {};
    const wifi = payload?.wifi || {};
    const errors = Array.isArray(payload?.errors) ? payload.errors.slice(0, 10) : [];
    const control = payload?.control || {};
    return {
        overallHealth: payload?.overallHealth,
        system: {
            uptimeSec: system.uptime,
            freeHeap: system.freeHeap,
            cpuTemp: system.cpuTemp
        },
        wifi: { rssi: wifi.rssi, connected: wifi.connected },
        control: {
            cycleLastMs: control.cycle?.lastMs,
            cycleMaxMs: control.cycle?.maxMs,
            commandsDropped: control.commands?.dropped,
            outputsRejected: control.outputs?.rejected
        },
        errors
    };
}

function truncate(text) {
    if (text.length <= MAX_TOOL_RESULT_CHARS) return text;
    return `${text.slice(0, MAX_TOOL_RESULT_CHARS)}\n…(результат обрезан)`;
}

export async function runTool(name, args = {}) {
    switch (name) {
        case 'get_status': {
            const status = await fetchJson('/api/status');
            const pack = buildStatePack(status);
            return { summary: 'текущее состояние', text: truncate(JSON.stringify(pack)) };
        }
        case 'get_health': {
            const health = await fetchJson('/api/health');
            return { summary: 'здоровье подсистем', text: truncate(JSON.stringify(packHealth(health))) };
        }
        case 'list_processes': {
            const limit = Math.min(20, Math.max(1, Number(args.limit) || 10));
            const history = await fetchJson('/api/history');
            const processes = (history?.processes || []).slice(0, limit).map((item) => ({
                id: item.id,
                type: item.type,
                profile: item.profile || null,
                durationSec: item.duration,
                status: item.status,
                totalVolumeMl: item.totalVolume,
                startedAt: item.startTime
            }));
            return {
                summary: `прогонов найдено: ${history?.total ?? processes.length}`,
                text: truncate(JSON.stringify({ total: history?.total ?? processes.length, processes }))
            };
        }
        case 'get_process': {
            const id = String(args.id || '').trim();
            if (!/^[0-9A-Za-z_-]+$/.test(id)) {
                throw new Error('Некорректный ID прогона');
            }
            const history = await fetchJson(`/api/history/${encodeURIComponent(id)}`);
            const pack = buildRunPack(history);
            return { summary: `прогон ${id}`, text: truncate(JSON.stringify(pack)) };
        }
        case 'compare_runs': {
            const baseId = String(args.baseId || '').trim();
            const compareId = String(args.compareId || '').trim();
            if (!baseId || !compareId) throw new Error('Нужны оба ID прогонов');
            const [base, compare] = await Promise.all([
                fetchJson(`/api/history/${encodeURIComponent(baseId)}`),
                fetchJson(`/api/history/${encodeURIComponent(compareId)}`)
            ]);
            const delta = compareRunPacks(buildRunPack(base), buildRunPack(compare));
            return { summary: `${baseId} → ${compareId}`, text: truncate(JSON.stringify(delta)) };
        }
        case 'get_logs': {
            const limit = Math.min(100, Math.max(1, Number(args.limit) || 40));
            const payload = await fetchJson(`/api/logs/events?limit=${limit}`);
            const digest = buildLogDigest(payload, limit);
            return {
                summary: `событий: ${payload?.count ?? digest.tail.length}`,
                text: truncate(JSON.stringify(digest))
            };
        }
        case 'get_live_chart': {
            const payload = await fetchJson('/api/charts/live');
            const pack = packChart(payload);
            return { summary: 'живые графики', text: truncate(JSON.stringify(pack)) };
        }
        default:
            throw new Error(`Неизвестный инструмент: ${name}`);
    }
}

export function toolSummary(name, args, result, error) {
    if (error) return `${name}: ошибка — ${error.message}`;
    return `${name}: ${result.summary}`;
}
