import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

test.beforeEach(async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);
  await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false),
  });
  await page.goto('/index.html');
  await page.evaluate(() => {
    document.querySelectorAll('.tab[data-tab="control"]')[0]?.click();
  });
  await page.waitForSelector('#mode-start-button', { state: 'visible' });
});

test('mode buttons reflect active mode and pause/resume state', async ({ page }) => {
  const btnRect = page.locator('button[data-mode-action="rectification"]');
  const btnManual = page.locator('button[data-mode-action="manual"]');
  const btnDist = page.locator('button[data-mode-action="distillation"]');
  const btnMash = page.locator('button[data-mode-action="mashing"]');
  const btnHold = page.locator('button[data-mode-action="hold"]');
  const startButton = page.locator('#mode-start-button');
  const pauseResumeBtn = page.locator('#runtime-pause-resume-btn');

  await expect(btnRect).toBeEnabled();
  await expect(btnManual).toBeEnabled();
  await expect(btnDist).toBeEnabled();
  await expect(btnMash).toBeEnabled();
  await expect(btnHold).toBeEnabled();

  await page.evaluate(() => {
    selectControlMode('distillation');
    window.__mockWs.emit({ mode: 2, paused: false, phase: 0 });
  });

  await expect(btnDist).toContainText('Running:');
  await expect(btnDist).toHaveClass(/btn-active-mode/);
  await expect(btnRect).toBeDisabled();
  await expect(btnManual).toBeDisabled();
  await expect(btnMash).toBeDisabled();
  await expect(btnHold).toBeDisabled();
  await expect(startButton).toBeDisabled();
  await expect(startButton).toHaveAttribute('data-mode', '2');
  await expect(pauseResumeBtn).toBeEnabled();
  await expect(pauseResumeBtn).toHaveAttribute('data-runtime-action', 'pause');

  await page.evaluate(() => window.__mockWs.emit({ mode: 2, paused: true, phase: 0 }));
  await expect(pauseResumeBtn).toHaveAttribute('data-runtime-action', 'resume');
});

test('busy runtime blocks both switching and same-mode restart until idle', async ({ page }) => {
  const switchAttempt = await page.evaluate(async () => {
    let confirmCalls = 0;
    const processUrls = [];

    const originalConfirm = window.confirm;
    const originalFetch = window.fetch;

    window.confirm = () => {
      confirmCalls += 1;
      return false;
    };
    window.fetch = async (...args) => {
      const url = String(args[0]);
      if (url.includes('/api/process/')) {
        processUrls.push(url);
      }
      return originalFetch(...args);
    };

    window.__mockWs.emit({ mode: 2, paused: false, phase: 0 });
    selectControlMode('rectification');
    await startSelectedMode();

    window.confirm = originalConfirm;
    window.fetch = originalFetch;
    return { confirmCalls, processUrls };
  });

  expect(switchAttempt.confirmCalls).toBe(0);
  expect(switchAttempt.processUrls).toEqual([]);

  const sameModeAttempt = await page.evaluate(async () => {
    let confirmCalls = 0;
    const processUrls = [];

    const originalConfirm = window.confirm;
    const originalFetch = window.fetch;

    window.confirm = () => {
      confirmCalls += 1;
      return true;
    };
    window.fetch = async (...args) => {
      const url = String(args[0]);
      if (url.includes('/api/process/')) {
        processUrls.push(url);
      }
      return originalFetch(...args);
    };

    window.__mockWs.emit({ mode: 2, paused: false, phase: 0 });
    selectControlMode('distillation');
    await startSelectedMode();

    window.confirm = originalConfirm;
    window.fetch = originalFetch;
    return { confirmCalls, processUrls };
  });

  expect(sameModeAttempt.confirmCalls).toBe(0);
  expect(sameModeAttempt.processUrls).toEqual([]);
});
