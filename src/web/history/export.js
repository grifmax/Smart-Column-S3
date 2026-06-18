import { addLog } from '../core/logs.js';

const ANONYMIZED_REPORT_SCHEMA = 'smart-column-anonymized-run-report-v1';

function sanitizeFilenameToken(value, fallback = 'run') {

    const token = String(value || '')
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9_-]+/g, '-')
        .replace(/^-+|-+$/g, '');

    return token || fallback;

}

function buildDurationSec(process) {

    const startTime = Number(process?.metadata?.startTime || process?.startTime || 0);
    const endTime = Number(process?.metadata?.endTime || process?.endTime || 0);

    if (startTime > 0 && endTime >= startTime) {
        return Math.max(0, Math.round(endTime - startTime));
    }

    const explicitDuration = Number(process?.metadata?.durationSec || process?.durationSec || process?.duration || 0);
    return Number.isFinite(explicitDuration) && explicitDuration > 0 ? Math.round(explicitDuration) : 0;

}

function buildSafeTimeseries(process) {

    const raw = Array.isArray(process?.timeseries?.data) ? process.timeseries.data : [];

    if (!raw.length) {
        return null;
    }

    return {
        sampleCount: raw.length,
        points: raw.map((point, index) => ({
            index,
            relativeTimeSec: Number(point?.time || 0),
            cubeC: Number(point?.cube || 0),
            columnTopC: Number(point?.columnTop || 0),
            refluxC: Number(point?.reflux || 0),
            powerW: Number(point?.power || 0),
            pressureMmHg: Number(point?.pressure || 0),
            speedMlH: Number(point?.speed || 0)
        }))
    };

}

function buildSafePhases(process) {

    const phases = Array.isArray(process?.phases) ? process.phases : [];

    return phases.map((phase, index) => ({
        index,
        name: String(phase?.name || '').trim(),
        durationSec: Math.max(0, Math.round(Number(phase?.duration || 0))),
        volumeMl: Number(phase?.volume || 0),
        avgSpeedMlH: Number(phase?.avgSpeed || 0),
        reasonCode: String(phase?.reasonCode || '').trim(),
        operatorMessage: String(phase?.operatorMessage || '').trim()
    }));

}

function buildSafeSafety(process) {

    const safetySummary = process?.indicatorsSummary?.safety || process?.process?.safety || process?.safety || {};
    const lastReasonCode = String(
        process?.process?.lastReasonCode
        || process?.lastReasonCode
        || process?.v2?.lastReasonCode
        || ''
    ).trim();

    return {
        lastReasonCode,
        safetySummary: safetySummary && typeof safetySummary === 'object' ? { ...safetySummary } : null,
        alarmCount: Number(process?.metadata?.alarmCount || process?.alarmCount || 0),
        safetyTripCount: Number(process?.metadata?.safetyTripCount || process?.safetyTripCount || 0)
    };

}

function buildSafeAdvisorSnapshot(process) {

    const advisor = process?.advisorSnapshot;
    if (!advisor || typeof advisor !== 'object') {
        return null;
    }

    return {
        verdict: advisor?.verdict && typeof advisor.verdict === 'object' ? { ...advisor.verdict } : null,
        highlights: Array.isArray(advisor?.highlights) ? advisor.highlights.map((item) => ({ ...item })) : [],
        recommendations: Array.isArray(advisor?.recommendations) ? advisor.recommendations.map((item) => ({
            title: String(item?.title || '').trim(),
            detail: String(item?.detail || '').trim(),
            action: String(item?.action || '').trim(),
            tone: String(item?.tone || '').trim(),
            parameter: String(item?.parameter || '').trim(),
            suggestion: item?.suggestion && typeof item.suggestion === 'object' ? { ...item.suggestion } : null
        })) : [],
        compareSummary: advisor?.compareSummary && typeof advisor.compareSummary === 'object' ? { ...advisor.compareSummary } : null,
        baselineProfile: String(advisor?.baselineProfile || '').trim(),
        learningApplied: Boolean(advisor?.learningApplied)
    };

}

function buildAnonymizedHistoryReport(process) {

    const processType = String(process?.process?.type || process?.type || '').trim();
    const processStatus = String(process?.process?.status || process?.status || '').trim();
    const profileName = String(process?.process?.profile || process?.profile || '').trim();

    return {
        schema: ANONYMIZED_REPORT_SCHEMA,
        exportedAt: new Date().toISOString(),
        privacy: {
            sanitized: true,
            removed: [
                'internal run id',
                'absolute timestamps',
                'device/network identifiers',
                'local paths and export-only metadata'
            ]
        },
        run: {
            type: processType,
            status: processStatus,
            completedSuccessfully: Boolean(process?.completedSuccessfully || processStatus === 'completed'),
            durationSec: buildDurationSec(process),
            profileName: profileName || 'Unnamed profile',
            summary: process?.summary && typeof process.summary === 'object' ? { ...process.summary } : null,
            results: process?.results && typeof process.results === 'object' ? { ...process.results } : null,
            indicatorsSummary: process?.indicatorsSummary && typeof process.indicatorsSummary === 'object'
                ? { ...process.indicatorsSummary }
                : null,
            safety: buildSafeSafety(process),
            phases: buildSafePhases(process),
            advisorSnapshot: buildSafeAdvisorSnapshot(process),
            timeseries: buildSafeTimeseries(process)
        }
    };

}

function downloadJsonFile(payload, filename) {

    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);

}

async function loadHistoryProcess(id) {

    const response = await fetch(`/api/history/${id}`, { cache: 'no-store' });
    if (!response.ok) {
        throw new Error('Не удалось загрузить процесс для анонимизированного отчёта');
    }
    return response.json();

}

export async function exportHistory(id, format = null) {

    try {

        if (!format) {
            const choice = confirm('Выберите формат экспорта:\n\nОК - CSV (таблица)\nОтмена - JSON (данные)');
            format = choice ? 'csv' : 'json';
        }

        addLog(`📥 Экспорт процесса ${id} в формате ${format.toUpperCase()}...`, 'info');
        window.open(`/api/history/${id}/export?format=${format}`, '_blank');
        addLog(`✅ Экспорт процесса ${id} начат`, 'info');

    } catch (error) {

        console.error('Error exporting history:', error);
        addLog('✗ Ошибка экспорта', 'error');

    }

}

export async function exportHistoryCSV(id) {

    await exportHistory(id, 'csv');

}

export async function exportHistoryJSON(id) {

    await exportHistory(id, 'json');

}

export async function exportHistoryAnonymized(id, processSnapshot = null) {

    try {

        addLog(`🕶️ Готовим anonymized run report для ${id}...`, 'info');

        const process = processSnapshot && typeof processSnapshot === 'object'
            ? processSnapshot
            : await loadHistoryProcess(id);

        const payload = buildAnonymizedHistoryReport(process);
        const safeType = sanitizeFilenameToken(payload?.run?.type, 'run');
        const timestamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');

        downloadJsonFile(payload, `run_report_${safeType}_anonymized_${timestamp}.json`);
        addLog(`✅ Anonymized run report для ${id} сохранён`, 'success');

    } catch (error) {

        console.error('Error exporting anonymized history report:', error);
        addLog('❌ Ошибка экспорта anonymized run report', 'error');

    }

}
