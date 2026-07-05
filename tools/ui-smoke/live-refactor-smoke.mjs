import { chromium } from 'playwright';

const baseUrl = process.argv[2] || 'http://192.168.3.152';
const testName = `codex-smoke-${Date.now()}`;

const consoleIssues = [];
const pageErrors = [];

async function api(path, options = {}) {
    const response = await fetch(`${baseUrl}${path}`, options);
    const contentType = response.headers.get('content-type') || '';
    const body = contentType.includes('application/json')
        ? await response.json()
        : await response.text();
    return {
        ok: response.ok,
        status: response.status,
        contentType,
        body
    };
}

async function poll(label, fn, isDone, timeoutMs = 15000, stepMs = 500) {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) {
        const value = await fn();
        if (isDone(value)) {
            return value;
        }
        await new Promise((resolve) => setTimeout(resolve, stepMs));
    }
    throw new Error(`Timeout while waiting for ${label}`);
}

const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ acceptDownloads: true });
const page = await context.newPage();

page.on('console', (msg) => {
    if (msg.type() !== 'error') {
        return;
    }
    const text = msg.text();
    if (text.includes('404') || text.includes('favicon')) {
        return;
    }
    consoleIssues.push(text);
});
page.on('pageerror', (error) => {
    pageErrors.push(String(error));
});

await page.addInitScript(() => {
    window.__alerts = [];
    window.alert = (message) => window.__alerts.push(`ALERT:${String(message)}`);
    window.confirm = (message) => {
        window.__alerts.push(`CONFIRM:${String(message)}`);
        return true;
    };
});

try {
    await page.goto(`${baseUrl}/index.html`, { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForTimeout(3000);

    const versionInfo = await api('/api/version');
    if (!versionInfo.ok || !versionInfo.body?.firmware?.version || !versionInfo.body?.frontend?.version) {
        throw new Error(`Version API failed: ${JSON.stringify(versionInfo.body)}`);
    }

    const healthInfo = await api('/api/health');
    if (!healthInfo.ok || typeof healthInfo.body?.overallHealth !== 'number') {
        throw new Error(`Health API failed: ${JSON.stringify(healthInfo.body)}`);
    }

    const statusInfo = await api('/api/status');
    if (!statusInfo.ok || typeof statusInfo.body?.modeStr !== 'string' || !statusInfo.body?.v2?.available) {
        throw new Error(`Status API failed: ${JSON.stringify(statusInfo.body)}`);
    }

    await poll(
        'websocket connection indicator',
        async () => page.evaluate(() => document.getElementById('connection-text')?.textContent?.trim() || ''),
        (text) => text.includes('Подключ')
    );

    const wifiStatus = await api('/api/wifi/status');
    if (!wifiStatus.ok || typeof wifiStatus.body?.connected !== 'boolean' || typeof wifiStatus.body?.savedProfiles !== 'number') {
        throw new Error(`Wi-Fi status failed: ${JSON.stringify(wifiStatus.body)}`);
    }

    const wifiProfiles = await api('/api/wifi/profiles');
    if (!wifiProfiles.ok || !Array.isArray(wifiProfiles.body?.profiles)) {
        throw new Error(`Wi-Fi profiles failed: ${JSON.stringify(wifiProfiles.body)}`);
    }

    const wifiScan = await api('/api/wifi/scan');
    if (!wifiScan.ok || typeof wifiScan.body?.count !== 'number' || !Array.isArray(wifiScan.body?.networks)) {
        throw new Error(`Wi-Fi scan failed: ${JSON.stringify(wifiScan.body)}`);
    }

    const calibrationSnapshot = await api('/api/calibration');
    if (!calibrationSnapshot.ok || !Array.isArray(calibrationSnapshot.body?.temperatures) || !calibrationSnapshot.body?.pump) {
        throw new Error(`Calibration snapshot failed: ${JSON.stringify(calibrationSnapshot.body)}`);
    }

    const calibrationScan = await api('/api/calibration/scan');
    if (!calibrationScan.ok || typeof calibrationScan.body?.count !== 'number' || !Array.isArray(calibrationScan.body?.sensors)) {
        throw new Error(`Calibration scan failed: ${JSON.stringify(calibrationScan.body)}`);
    }

    const calibrationScanRaw = await api('/api/calibration/scan/raw');
    if (!calibrationScanRaw.ok || calibrationScanRaw.body?.success !== true ||
        !Array.isArray(calibrationScanRaw.body?.searchSensors) ||
        !Array.isArray(calibrationScanRaw.body?.runtimeSensors)) {
        throw new Error(`Calibration raw scan failed: ${JSON.stringify(calibrationScanRaw.body)}`);
    }

    const preflightResponse = await api('/api/process/preflight', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            mode: 'distillation',
            params: {
                speed: 500,
                headsVolume: 0,
                targetVolume: 3000,
                endTemp: 98,
                powerPercent: 100
            }
        })
    });
    if (!preflightResponse.ok || preflightResponse.body?.success !== true || !Array.isArray(preflightResponse.body?.items)) {
        throw new Error(`Process preflight failed: ${JSON.stringify(preflightResponse.body)}`);
    }

    await page.evaluate(() => {
        document.querySelectorAll('.tab[data-tab="profiles"]')[0]?.click();
    });
    await page.waitForTimeout(1200);

    await page.evaluate(async () => {
        await showCreateProfileModal();
    });
    await page.waitForSelector('#profile-modal', { state: 'visible', timeout: 10000 });
    await page.fill('#profile-name', testName);
    await page.fill('#profile-description', 'codex live smoke profile');
    await page.fill('#profile-tags', 'codex, smoke');
    await page.waitForTimeout(600);

    const summaryText = await page.locator('#profile-editor-summary').innerText();
    if (!summaryText.includes(testName)) {
        throw new Error('Profile summary did not render created profile name');
    }

    await page.evaluate(async () => {
        await saveProfile();
    });

    const createdList = await poll(
        'profile creation',
        () => api('/api/profiles'),
        (response) => Array.isArray(response.body?.profiles) &&
            response.body.profiles.some((item) => item.name === testName)
    );
    const createdProfile = createdList.body.profiles.find((item) => item.name === testName);
    const createdId = createdProfile.id;

    await page.evaluate(async (id) => {
        await showEditProfileModal(id);
    }, createdId);
    await page.waitForSelector('#profile-modal', { state: 'visible', timeout: 10000 });
    await page.fill('#profile-description', 'codex live smoke profile updated');
    await page.evaluate(async () => {
        await saveProfile();
    });

    const updatedProfile = await poll(
        'profile update',
        () => api(`/api/profiles/${createdId}`),
        (response) => response.body?.metadata?.description === 'codex live smoke profile updated'
    );

    await page.evaluate(async (id) => {
        await quickLoadProfile(id);
    }, createdId);
    await page.waitForTimeout(1200);

    const exportSingle = await api(`/api/profiles/${createdId}/export`);
    if (!exportSingle.ok || exportSingle.body?.metadata?.name !== testName) {
        throw new Error('Single profile export returned unexpected payload');
    }

    const exportAll = await api('/api/profiles/export');
    if (!exportAll.ok || !Array.isArray(exportAll.body) || !exportAll.body.some((item) => item.id === createdId)) {
        throw new Error('Batch profile export did not include created profile');
    }

    await new Promise((resolve) => setTimeout(resolve, 1200));
    const importResponse = await api('/api/profiles/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(exportSingle.body)
    });
    if (!importResponse.ok || !importResponse.body?.success || importResponse.body.imported < 1) {
        throw new Error(`Profile import failed: ${JSON.stringify(importResponse.body)}`);
    }

    const importedList = await poll(
        'profile import',
        () => api('/api/profiles'),
        (response) => Array.isArray(response.body?.profiles) &&
            response.body.profiles.filter((item) => item.name === testName).length >= 2
    );
    const importedIds = importedList.body.profiles
        .filter((item) => item.name === testName)
        .map((item) => item.id)
        .filter((id) => id !== createdId);

    for (const id of importedIds) {
        await page.evaluate((profileId) => {
            deleteProfile(profileId);
        }, id);
    }
    await page.evaluate((profileId) => {
        deleteProfile(profileId);
    }, createdId);

    await poll(
        'profile cleanup',
        () => api('/api/profiles'),
        (response) => Array.isArray(response.body?.profiles) &&
            response.body.profiles.every((item) => item.name !== testName)
    );

    const seededHistory = await api('/api/history/demo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ replace: true })
    });
    if (!seededHistory.ok || !seededHistory.body?.success || !(seededHistory.body.demoCount > 0)) {
        throw new Error(`History demo seed failed: ${JSON.stringify(seededHistory.body)}`);
    }

    await page.evaluate(() => {
        document.querySelectorAll('.tab[data-tab="history"]')[0]?.click();
    });
    await page.waitForSelector('.history-item', { timeout: 15000 });
    await page.locator('button[onclick^="viewHistoryDetails("]').first().click();
    await page.waitForTimeout(1200);

    const historyModalVisible = await page.evaluate(() => {
        const modal = document.getElementById('history-modal');
        return Boolean(modal) &&
            (modal.classList.contains('active') || modal.style.display === 'flex');
    });
    if (!historyModalVisible) {
        throw new Error('History details modal did not open');
    }

    const historyList = await api('/api/history');
    if (!historyList.ok || !(historyList.body?.total > 0) || !Array.isArray(historyList.body?.processes)) {
        throw new Error('History list is empty after seeding demo dataset');
    }
    const historyId = historyList.body.processes[0]?.id;
    if (!historyId) {
        throw new Error('History item id is missing');
    }

    const historyDetails = await api(`/api/history/${historyId}`);
    if (!historyDetails.ok || !historyDetails.body?.id) {
        throw new Error(`History details failed: ${JSON.stringify(historyDetails.body)}`);
    }

    const historyExportJson = await api(`/api/history/${historyId}/export?format=json`);
    if (!historyExportJson.ok || !historyExportJson.contentType.includes('application/json') ||
        !historyExportJson.body?.metadata) {
        throw new Error('History JSON export failed');
    }

    const historyExportCsv = await api(`/api/history/${historyId}/export?format=csv`);
    if (!historyExportCsv.ok || !historyExportCsv.contentType.includes('text/csv') ||
        !String(historyExportCsv.body).includes('Time,Cube Temp')) {
        throw new Error('History CSV export failed');
    }

    const clearedHistory = await api('/api/history/demo', { method: 'DELETE' });
    if (!clearedHistory.ok || !clearedHistory.body?.success) {
        throw new Error(`History demo clear failed: ${JSON.stringify(clearedHistory.body)}`);
    }

    const finalHistory = await poll(
        'history cleanup',
        () => api('/api/history'),
        (response) => Number(response.body?.total || 0) === 0
    );

    const alerts = await page.evaluate(() => Array.isArray(window.__alerts) ? window.__alerts.slice() : []);

    console.log(JSON.stringify({
        ok: true,
        baseUrl,
        createdId,
        importedIds,
        historyId,
        updatedDescription: updatedProfile.body?.metadata?.description || '',
        finalHistoryTotal: finalHistory.body?.total || 0,
        alerts,
        consoleIssues,
        pageErrors
    }, null, 2));
} finally {
    await browser.close();
}
