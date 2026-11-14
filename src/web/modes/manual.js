import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';
import { runtimeMonitorState } from '../globals.js';

async function submitProcessCommand(action) {
    const response = await fetch(`/api/process/${action}`, { method: 'POST' });
    const data = await response.json().catch(() => ({}));
    if (!response.ok || !data.accepted) {
        throw new Error(data.message || `HTTP ${response.status}`);
    }
    if (!data.queued || !data.commandId) return data.status || 'applied';

    const terminal = new Set(['applied', 'rejected', 'cancelled', 'superseded', 'queue_full']);
    const deadline = Date.now() + 2000;
    while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 100));
        const statusResponse = await fetch(
            `/api/process/command-status?commandId=${encodeURIComponent(data.commandId)}`,
            { cache: 'no-store' }
        );
        if (!statusResponse.ok) continue;
        const statusData = await statusResponse.json().catch(() => ({}));
        if (!terminal.has(statusData.status)) continue;
        if (statusData.status !== 'applied') {
            throw new Error(`Command ${action}: ${statusData.status}`);
        }
        return statusData.status === 'applied' ? 'applied' : statusData.status;
    }
    return 'queued';
}

async function submitOutputResponse(response, actionLabel) {
    const data = await response.json().catch(() => ({}));
    const commands = Array.isArray(data.commands)
        ? data.commands
        : data.commandId ? [data] : [];
    if (!response.ok && commands.length === 0) {
        throw new Error(data.message || `${actionLabel}: HTTP ${response.status}`);
    }
    const failures = [];
    for (const command of commands) {
        if (command.status === 'rejected') {
            failures.push(command.rejectReason || command.status);
            continue;
        }
        if (!command.queued || !command.commandId) continue;
        const deadline = Date.now() + 2000;
        let terminalStatus = 'queued';
        while (Date.now() < deadline) {
            await new Promise((resolve) => setTimeout(resolve, 100));
            const endpoint = command.commandType === 'control'
                ? '/api/process/command-status'
                : '/api/outputs/command';
            const statusResponse = await fetch(
                `${endpoint}?commandId=${encodeURIComponent(command.commandId)}`,
                { cache: 'no-store' }
            );
            if (!statusResponse.ok) continue;
            const status = await statusResponse.json().catch(() => ({}));
            if (status.status === 'applied') {
                terminalStatus = 'applied';
                break;
            }
            if (['rejected', 'cancelled', 'superseded', 'queue_full'].includes(status.status)) {
                terminalStatus = status.rejectReason || status.status;
                break;
            }
        }
        if (terminalStatus !== 'applied') failures.push(terminalStatus);
    }
    if (data.accepted === false || data.status === 'rejected') {
        failures.push(data.rejectReason || data.status || 'rejected');
    }
    if (!response.ok && failures.length === 0) {
        failures.push(data.message || `HTTP ${response.status}`);
    }
    if (failures.length) throw new Error(`${actionLabel}: ${failures.join(', ')}`);
    return data;
}

export async function stopProcess() {
    if (!confirm('РћСЃС‚Р°РЅРѕРІРёС‚СЊ РїСЂРѕС†РµСЃСЃ?')) return;

    try {
        const status = await submitProcessCommand('stop');
        if (status === 'queued') {
            addLog('Stop command accepted; waiting for control loop', 'warning');
            setTimeout(loadStatus, 500);
            return;
        }

        if (status === 'applied') {
            addLog('вњ“ РџСЂРѕС†РµСЃСЃ РѕСЃС‚Р°РЅРѕРІР»РµРЅ', 'warning');
            setTimeout(loadStatus, 500);
        } else {
            addLog('вњ— РћС€РёР±РєР° РѕСЃС‚Р°РЅРѕРІРєРё', 'error');
        }
    } catch (e) {
        addLog('вњ— РћС€РёР±РєР°: ' + e.message, 'error');
    }
}

export async function pauseProcess() {
    try {
        const status = await submitProcessCommand('pause');
        if (status === 'queued') {
            addLog('Pause command accepted; waiting for control loop', 'info');
            setTimeout(loadStatus, 500);
            return;
        }

        if (status === 'applied') {
            addLog('вњ“ РџСЂРѕС†РµСЃСЃ РїСЂРёРѕСЃС‚Р°РЅРѕРІР»РµРЅ', 'info');
            setTimeout(loadStatus, 500);
        } else {
            addLog('вњ— РћС€РёР±РєР° РїР°СѓР·С‹', 'error');
        }
    } catch (e) {
        addLog('вњ— РћС€РёР±РєР°: ' + e.message, 'error');
    }
}

export async function resumeProcess() {
    try {
        const status = await submitProcessCommand('resume');
        if (status === 'queued') {
            addLog('Resume command accepted; waiting for control loop', 'info');
            setTimeout(loadStatus, 500);
            return;
        }

        if (status === 'applied') {
            addLog('вњ“ РџСЂРѕС†РµСЃСЃ РІРѕР·РѕР±РЅРѕРІР»РµРЅ', 'info');
            setTimeout(loadStatus, 500);
        } else {
            addLog('вњ— РћС€РёР±РєР° РІРѕР·РѕР±РЅРѕРІР»РµРЅРёСЏ', 'error');
        }
    } catch (e) {
        addLog('вњ— РћС€РёР±РєР°: ' + e.message, 'error');
    }
}

export async function updateHeater(value) {
    document.getElementById('heater-value').textContent = value;

    try {
        const response = await fetch('/api/manual/heater', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ powerW: parseInt(value, 10) || 0 })
        });
        await submitOutputResponse(response, 'Heater command');
    } catch (e) {
        addLog('вњ— РћС€РёР±РєР° СѓСЃС‚Р°РЅРѕРІРєРё РјРѕС‰РЅРѕСЃС‚Рё: ' + e.message, 'error');
    }
}

export function updatePump(value) {
    document.getElementById('pump-value').textContent = value;

    fetch('/api/manual/pump', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ speed: parseInt(value, 10) || 0 })
    })
        .then(async (response) => {
            await submitOutputResponse(response, 'Pump command');
            runtimeMonitorState.pump = {
                ...runtimeMonitorState.pump,
                speedMlH: parseInt(value, 10) || 0
            };
            setTimeout(loadStatus, 300);
        })
        .catch((e) => {
            addLog('вњ— РћС€РёР±РєР° СѓСЃС‚Р°РЅРѕРІРєРё РЅР°СЃРѕСЃР°: ' + e.message, 'error');
        });
}

export function toggleValve(name) {
    const currentState = Boolean(runtimeMonitorState?.valves?.[name]);
    const nextState = !currentState;

    fetch('/api/manual/valves', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ [name]: nextState })
    })
        .then(async (response) => {
            await submitOutputResponse(response, 'Valve command');
            runtimeMonitorState.valves = {
                ...runtimeMonitorState.valves,
                [name]: nextState
            };
            addLog(`рџ”„ РљР»Р°РїР°РЅ ${name}: ${nextState ? 'open' : 'closed'}`);
            setTimeout(loadStatus, 300);
        })
        .catch((e) => {
            addLog('вњ— РћС€РёР±РєР° СѓРїСЂР°РІР»РµРЅРёСЏ РєР»Р°РїР°РЅРѕРј: ' + e.message, 'error');
        });
}
