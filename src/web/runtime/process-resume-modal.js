import { isCloudProxyMode } from '../core/cloud-detect.js';
import { loadStatus } from '../core/status.js';

const phaseLabels = {
    heating: 'нагрев',
    stabilization: 'стабилизация',
    heads: 'отбор голов',
    post_heads: 'стабилизация после голов',
    body: 'отбор тела',
    tails: 'отбор хвостов',
    purge: 'продувка'
};

let pendingSnapshot = null;

function setError(message = '') {
    const error = document.getElementById('process-resume-error');
    if (!error) return;
    error.textContent = message;
    error.hidden = !message;
}

function showModal() {
    document.getElementById('process-resume-modal')?.classList.add('active');
}

function hideModal() {
    document.getElementById('process-resume-modal')?.classList.remove('active');
    setError();
}

function renderSnapshot(snapshot) {
    pendingSnapshot = snapshot;
    const summary = document.getElementById('process-resume-summary');
    const recoveryGroup = document.getElementById('process-resume-heads-group');
    const recoveryInput = document.getElementById('process-resume-heads-ml');
    const recoveryHint = document.getElementById('process-resume-heads-hint');
    const phase = phaseLabels[snapshot.phase] || snapshot.phase || 'неизвестна';
    const volumes = snapshot.volumes || {};
    const recovery = snapshot.recoveryHeads || {};
    let guidance = '';
    if (recovery.offered) {
        guidance = ' После повторного прогрева и стабилизации будет выполнен выбранный дополнительный отбор голов, затем продолжится тело.';
    } else if (snapshot.phase === 'heads') {
        guidance = ' После повторного прогрева и стабилизации обычный отбор голов продолжится до прежней цели.';
    } else if (snapshot.phase === 'tails') {
        guidance = ' После повторного прогрева и стабилизации продолжится отбор хвостов, без дополнительных голов.';
    }

    if (summary) {
        summary.textContent = `Сохранён прерванный прогон: ${phase}. ` +
            `Головы ${Math.round(volumes.heads || 0)} мл, ` +
            `тело ${Math.round(volumes.body || 0)} мл, ` +
            `хвосты ${Math.round(volumes.tails || 0)} мл.${guidance}`;
    }

    const offered = Boolean(recovery.offered);
    if (recoveryGroup) recoveryGroup.hidden = !offered;
    if (offered && recoveryInput) {
        recoveryInput.value = Math.round(
            recovery.selectedTargetMl ?? recovery.suggestedMl ?? 0
        );
        recoveryInput.min = String(Math.ceil(recovery.collectedMl || 0));
        recoveryInput.max = '100';
        if (recoveryHint) {
            const collected = Number(recovery.collectedMl || 0);
            const suggested = Number(recovery.suggestedMl || 0);
            recoveryHint.textContent = collected > 0
                ? `Уже отобрано ${collected.toFixed(1)} мл из выбранных. Цель нельзя уменьшить ниже уже отобранного объёма.`
                : `Рекомендация: ${Math.round(suggested)} мл. Можно задать от 0 до 100 мл.`;
        }
    }
    setError();
    showModal();
}

export async function checkPendingProcessResume() {
    if (isCloudProxyMode) return;
    for (let attempt = 0; attempt < 5; attempt += 1) {
        try {
            const response = await fetch('/api/process/resume-available', {
                cache: 'no-store'
            });
            if (response.ok) {
                const result = await response.json();
                if (result.available) renderSnapshot(result);
                return;
            }
            if (response.status !== 503) return;
        } catch (error) {
            if (attempt === 4) {
                console.warn('Could not check for a saved process:', error);
            }
        }
        await new Promise((resolve) => setTimeout(resolve, 300));
    }
}

async function waitForCommand(commandId) {
    if (!commandId) return 'applied';
    const terminal = new Set(['applied', 'rejected', 'cancelled', 'superseded', 'queue_full']);
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 120));
        const response = await fetch(
            `/api/process/command-status?commandId=${encodeURIComponent(commandId)}`,
            { cache: 'no-store' }
        );
        if (!response.ok) continue;
        const result = await response.json();
        if (terminal.has(result.status)) return result.status;
    }
    return 'queued';
}

async function submitRecovery() {
    if (!pendingSnapshot) return;
    const startButton = document.getElementById('process-resume-start');
    const discardButton = document.getElementById('process-resume-discard');
    const recovery = pendingSnapshot.recoveryHeads || {};
    const input = document.getElementById('process-resume-heads-ml');
    const additionalHeadsMl = recovery.offered ? Number(input?.value) : 0;
    if (!Number.isFinite(additionalHeadsMl) || additionalHeadsMl < Number(input?.min || 0) || additionalHeadsMl > 100) {
        setError('Проверьте объём дополнительных голов.');
        return;
    }

    startButton.disabled = true;
    discardButton.disabled = true;
    setError();
    try {
        const response = await fetch('/api/process/recover', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ additionalHeadsMl })
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.accepted) {
            throw new Error(result.message || `HTTP ${response.status}`);
        }
        const status = await waitForCommand(result.commandId);
        if (status !== 'applied' && status !== 'queued') {
            throw new Error(`Команда восстановления отклонена: ${status}`);
        }
        hideModal();
        pendingSnapshot = null;
        await loadStatus();
    } catch (error) {
        setError(error.message || 'Не удалось восстановить процесс.');
    } finally {
        startButton.disabled = false;
        discardButton.disabled = false;
    }
}

async function discardRecovery() {
    const startButton = document.getElementById('process-resume-start');
    const discardButton = document.getElementById('process-resume-discard');
    startButton.disabled = true;
    discardButton.disabled = true;
    setError();
    try {
        const response = await fetch('/api/process/resume/discard', { method: 'POST' });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.accepted) {
            throw new Error(result.message || `HTTP ${response.status}`);
        }
        const status = await waitForCommand(result.commandId);
        if (status !== 'applied' && status !== 'queued') {
            throw new Error(`Команда удаления снимка отклонена: ${status}`);
        }
        hideModal();
        pendingSnapshot = null;
    } catch (error) {
        setError(error.message || 'Не удалось удалить снимок процесса.');
    } finally {
        startButton.disabled = false;
        discardButton.disabled = false;
    }
}

export function initProcessResumeModal() {
    document.getElementById('process-resume-start')?.addEventListener('click', submitRecovery);
    document.getElementById('process-resume-discard')?.addEventListener('click', discardRecovery);
}
