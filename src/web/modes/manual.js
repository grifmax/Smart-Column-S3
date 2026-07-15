import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';
import { runtimeMonitorState } from '../globals.js';

export async function stopProcess() {
    if (!confirm('РћСЃС‚Р°РЅРѕРІРёС‚СЊ РїСЂРѕС†РµСЃСЃ?')) return;

    try {
        const response = await fetch('/api/process/stop', {
            method: 'POST'
        });

        if (response.ok) {
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
        const response = await fetch('/api/process/pause', {
            method: 'POST'
        });

        if (response.ok) {
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
        const response = await fetch('/api/process/resume', {
            method: 'POST'
        });

        if (response.ok) {
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
        if (!response.ok) throw new Error(await response.text());
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
            if (!response.ok) throw new Error(await response.text());
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
            if (!response.ok) throw new Error(await response.text());
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
