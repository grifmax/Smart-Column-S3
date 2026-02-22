import { test, expect } from '@playwright/test';

function statusPayload(mode, paused = false) {
  const modeMap = {
    0: 'idle',
    1: 'rectification',
    2: 'manual',
    3: 'distillation',
    4: 'mashing',
    5: 'hold'
  };

  return {
    mode,
    modeStr: modeMap[mode] || 'idle',
    phase: 0,
    phaseStr: 'idle',
    paused,
    uptime: 120,
    temps: {
      cube: 78.4,
      columnBottom: 77.1,
      columnTop: 76.8,
      reflux: 75.2,
      deflegmator: 23.0,
      product: 23.0,
      tsa: 45.0,
      waterIn: 20.0,
      waterOut: 24.0
    },
    pressure: { cube: 0, atm: 1013, kpa: 101.3 },
    power: { voltage: 230, current: 3.2, power: 736, energy: 0.1, frequency: 50, pf: 0.98 },
    pump: { speedMlH: 0, totalMl: 0, running: false },
    hydrometer: { abv: 0, density: 0, valid: false },
    volumes: { heads: 0, body: 0, tails: 0 },
    equipment: { heaterPowerW: 3000, columnHeightMm: 1500 }
  };
}

async function installApiMocks(page) {
  const versionPayload = {
    firmware: { version: 'test', buildDate: 'Feb 22 2026', buildTime: '00:00:00' },
    frontend: { buildDate: 'Feb 22 2026', buildTime: '00:00:00' }
  };

  await page.route('**/api/web/user', (route) => route.fulfill({ status: 404, body: 'not found' }));
  await page.route('**/api/version', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(versionPayload) }));
  await page.route('**/api/status', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(statusPayload(0, false)) }));
  await page.route('**/api/process/start', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: '{"success":true}' }));
  await page.route('**/api/**', (route) =>
    route.fulfill({ status: 200, contentType: 'application/json', body: '{}' }));
}

test.beforeEach(async ({ page }) => {
  await installApiMocks(page);
  await page.goto('/index.html');
  await page.click('button.tab[data-tab="control"]');
  await page.waitForSelector('button[onclick="startRectification()"]', { state: 'visible' });
  await page.evaluate(() => updateUI({ mode: 0, paused: false, phase: 0 }));
});

test('mode buttons reflect active mode and pause/resume state', async ({ page }) => {
  const btnRect = page.locator('button[onclick="startRectification()"]');
  const btnManual = page.locator('button[onclick="startManual()"]');
  const btnDist = page.locator('button[onclick="startDistillation()"]');
  const btnMash = page.locator('button[onclick="startMashing()"]');
  const btnHold = page.locator('button[onclick="startHold()"]');
  const btnPause = page.locator('button[onclick="pauseProcess()"]');
  const btnResume = page.locator('button[onclick="resumeProcess()"]');

  await expect(btnRect).toBeEnabled();
  await expect(btnManual).toBeEnabled();
  await expect(btnDist).toBeEnabled();
  await expect(btnMash).toBeEnabled();
  await expect(btnHold).toBeEnabled();

  await page.evaluate(() => updateUI({ mode: 3, paused: false, phase: 0 }));

  await expect(btnDist).toContainText('Running:');
  await expect(btnDist).toHaveClass(/btn-active-mode/);
  await expect(btnRect).toBeDisabled();
  await expect(btnManual).toBeDisabled();
  await expect(btnMash).toBeDisabled();
  await expect(btnHold).toBeDisabled();
  await expect(btnPause).toBeEnabled();
  await expect(btnResume).toBeDisabled();

  await page.evaluate(() => updateUI({ mode: 3, paused: true, phase: 0 }));
  await expect(btnPause).toBeDisabled();
  await expect(btnResume).toBeEnabled();
});

test('switch confirmation blocks request and same-mode restart is ignored', async ({ page }) => {
  const switchAttempt = await page.evaluate(async () => {
    let confirmCalls = 0;
    let fetchCalls = 0;

    const originalConfirm = window.confirm;
    const originalFetch = window.fetch;

    window.confirm = () => {
      confirmCalls += 1;
      return false;
    };
    window.fetch = async (...args) => {
      fetchCalls += 1;
      return originalFetch(...args);
    };

    updateUI({ mode: 3, paused: false, phase: 0 });
    await startRectification();

    window.confirm = originalConfirm;
    window.fetch = originalFetch;
    return { confirmCalls, fetchCalls };
  });

  expect(switchAttempt.confirmCalls).toBe(1);
  expect(switchAttempt.fetchCalls).toBe(0);

  const sameModeAttempt = await page.evaluate(async () => {
    let confirmCalls = 0;
    let fetchCalls = 0;

    const originalConfirm = window.confirm;
    const originalFetch = window.fetch;

    window.confirm = () => {
      confirmCalls += 1;
      return true;
    };
    window.fetch = async (...args) => {
      fetchCalls += 1;
      return originalFetch(...args);
    };

    updateUI({ mode: 3, paused: false, phase: 0 });
    await startDistillation();

    window.confirm = originalConfirm;
    window.fetch = originalFetch;
    return { confirmCalls, fetchCalls };
  });

  expect(sameModeAttempt.confirmCalls).toBe(0);
  expect(sameModeAttempt.fetchCalls).toBe(0);
});
