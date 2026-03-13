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
  await expect(startButton).toBeEnabled();
  await expect(startButton).toHaveAttribute('data-mode', '2');
  await expect(pauseResumeBtn).toBeEnabled();
  await expect(pauseResumeBtn).toHaveAttribute('data-runtime-action', 'pause');

  await page.evaluate(() => window.__mockWs.emit({ mode: 2, paused: true, phase: 0 }));
  await expect(pauseResumeBtn).toHaveAttribute('data-runtime-action', 'resume');
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

    window.__mockWs.emit({ mode: 2, paused: false, phase: 0 });
    selectControlMode('rectification');
    await startSelectedMode();

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

    window.__mockWs.emit({ mode: 2, paused: false, phase: 0 });
    selectControlMode('distillation');
    await startSelectedMode();

    window.confirm = originalConfirm;
    window.fetch = originalFetch;
    return { confirmCalls, fetchCalls };
  });

  expect(sameModeAttempt.confirmCalls).toBe(0);
  expect(sameModeAttempt.fetchCalls).toBe(0);
});
