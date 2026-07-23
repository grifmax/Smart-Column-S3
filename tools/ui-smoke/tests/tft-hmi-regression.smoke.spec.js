import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

async function openTab(page, tabId, selector = `.tab[data-tab="${tabId}"]`) {
  await page.locator(selector).first().click();
  await expect(page.locator(`#${tabId}`)).toHaveClass(/active/);
}

async function installSmokePage(page, options = {}) {
  await installMockWebSocket(page);
  await installMockApexCharts(page);
  const requests = await installCommonApiMocks(page, {
    statusPayload: options.statusPayload || buildStatusPayload(0, false),
    testingStatusPayload: options.testingStatusPayload,
  });
  await page.goto('/index.html');
  return requests;
}

test('new menu hierarchy stays reachable for RU and EN document locales', async ({ page }) => {
  await installSmokePage(page);

  for (const locale of ['ru', 'en']) {
    await page.evaluate((value) => {
      document.documentElement.lang = value;
    }, locale);
    await expect(page.locator('html')).toHaveAttribute('lang', locale);

    for (const tabId of ['monitor', 'control', 'equipment', 'safety', 'tools', 'settings', 'wifi', 'history']) {
      await openTab(page, tabId);
    }
    await openTab(page, 'profiles', '.sidebar-item.tab[data-tab="profiles"]');
  }
});

test('safety alarm blocks a dangerous start before it reaches the API', async ({ page }) => {
  const requests = await installSmokePage(page, {
    statusPayload: buildStatusPayload(0, false, {
      safetyOk: false,
      currentAlarm: {
        active: true,
        latched: true,
        type: 'overheat',
        typeCode: 8,
        acknowledged: false,
        message: 'Safety trip',
      },
      v2: {
        available: true,
        lifecycle: 'faulted',
        safetyLatched: true,
        indicators: { sensorFreshnessOk: true },
      },
    }),
  });

  await openTab(page, 'control');
  await page.waitForSelector('#mode-start-button', { state: 'visible' });
  await page.evaluate(() => selectControlMode('distillation'));

  await expect(page.locator('#mode-start-button')).toBeDisabled();
  await page.evaluate(() => startSelectedMode());
  expect(requests.processStarts).toHaveLength(0);
});

test('missing required sensor blocks start without issuing a process command', async ({ page }) => {
  const requests = await installSmokePage(page, {
    statusPayload: buildStatusPayload(0, false, {
      v2: {
        available: true,
        lifecycle: 'idle',
        safetyLatched: false,
        indicators: { sensorFreshnessOk: false },
      },
    }),
  });

  await openTab(page, 'control');
  await page.waitForSelector('#mode-start-button', { state: 'visible' });
  await page.evaluate(() => selectControlMode('rectification'));

  await expect(page.locator('#mode-start-button')).toBeDisabled();
  await page.evaluate(() => startSelectedMode());
  expect(requests.processStarts).toHaveLength(0);
});

test('service workspace remains reachable from pause without resuming the process', async ({ page }) => {
  const requests = await installSmokePage(page, {
    statusPayload: buildStatusPayload(2, true, {
      v2: {
        available: true,
        lifecycle: 'paused',
        safetyLatched: false,
        indicators: { sensorFreshnessOk: true },
      },
    }),
    testingStatusPayload: {
      success: true,
      processActive: true,
      testingAllowed: false,
      availabilityReason: 'Process is paused',
      safetyOk: true,
      safetyLatched: false,
      activeTests: {},
    },
  });

  await openTab(page, 'control');
  await expect(page.locator('#runtime-pause-resume-btn')).toHaveAttribute(
    'data-runtime-action', 'resume',
  );

  await openTab(page, 'equipment');
  await page.locator('[data-equipment-section-btn="testing"]').click();
  await expect(page.locator('[data-equipment-section-pane="testing"]')).toHaveClass(/active/);
  expect(requests.processActions.filter((entry) => entry.pathname === '/api/process/resume')).toHaveLength(0);
});

test('calibration can return to the service testing workspace', async ({ page }) => {
  await installSmokePage(page);
  await openTab(page, 'equipment');

  await page.locator('[data-equipment-section-btn="testing"]').click();
  await page.locator('.equipment-testing-nav-item[data-testing-card-id="pump"]').click();
  await expect(page.locator('#equipment-test-pump-open-calibration')).toBeVisible();
  await page.locator('#equipment-test-pump-open-calibration').click();

  await expect(page.locator('[data-equipment-section-pane="calibration"]')).toHaveClass(/active/);
  await expect(page.locator('#cal-speed')).toBeVisible();

  await page.locator('[data-equipment-section-btn="testing"]').click();
  await expect(page.locator('[data-equipment-section-pane="testing"]')).toHaveClass(/active/);
  await expect(page.locator('#equipment-test-pump-open-calibration')).toBeVisible();
});
