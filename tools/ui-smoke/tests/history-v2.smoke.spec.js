import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

const historyListPayload = {
  processes: [
    {
      id: 'process-v2-1',
      type: 'rectification',
      status: 'error',
      startTime: 1710489600,
      duration: 8100,
      totalVolume: 1650,
      safetySummary: 'ACK + RESET',
      safetyState: 'recovery',
      safetyTrip: true,
      safetyAck: true,
      safetyRecovery: true,
      safetyReset: true,
      completionState: 'safety_stop',
      completionReasonCode: 'RC_SAFETY_RESET_COMPLETED',
      completionOperatorMessage: 'Авария сброшена оператором',
      lastPhaseName: 'body',
      lastReasonCode: 'RC_BODY_END_DETECTED',
      lastOperatorMessage: 'Тело завершено по температуре',
    },
  ],
};

const historyDetailsPayloads = {
  'process-v2-1': {
    id: 'process-v2-1',
    process: {
      type: 'rectification',
      mode: 'auto',
    },
    metadata: {
      startTime: 1710489600,
      endTime: 1710497700,
      duration: 8100,
      completedSuccessfully: false,
    },
    metrics: {
      power: {
        avgPower: 1530,
        energyUsed: 3.44,
      },
    },
    timeseries: {
      data: [
        { time: 1710489600, cube: 78.1, columnTop: 77.0, power: 1500 },
        { time: 1710491400, cube: 79.0, columnTop: 77.5, power: 1600 },
        { time: 1710493200, cube: 80.2, columnTop: 78.0, power: 1490 },
      ],
    },
    phases: [
      {
        name: 'body',
        startTime: 1710490500,
        endTime: 1710495000,
        duration: 4500,
        volume: 1280,
        avgSpeed: 1024,
        reasonCode: 'RC_BODY_END_DETECTED',
        operatorMessage: 'Тело завершено по температуре',
      },
    ],
    results: {
      headsCollected: 120,
      bodyCollected: 1280,
      tailsCollected: 250,
      totalCollected: 1650,
      errors: [
        {
          time: 1710496200,
          severity: 'error',
          message: 'Перегрев дефлегматора',
          reasonCode: 'RC_SAFETY_TRIP_OVERHEAT',
        },
      ],
      warnings: [
        {
          time: 1710496500,
          severity: 'warning',
          message: 'Условия безопасности восстановлены',
          reasonCode: 'RC_SAFETY_RECOVERY_ENTERED',
          operatorMessage: 'Температура вернулась в норму',
        },
      ],
    },
    notes: 'Smoke-проверка v2 history details',
  },
};

test('history tab renders v2 safety summary and details modal', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);
  const requests = await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false),
    historyListPayload,
    historyDetailsPayloads,
  });

  await page.goto('/index.html');
  await page.evaluate(() => {
    document.querySelectorAll('.tab[data-tab="history"]')[0]?.click();
  });

  const historyItem = page.locator('.history-item').first();
  await expect(historyItem).toBeVisible();
  await expect(historyItem).toContainText('Safety ACK + RESET');
  await expect(historyItem).toContainText('SAFETY STOP');
  await expect(historyItem).toContainText('Отбор тела: Тело завершено по температуре');
  await expect.poll(() => requests.historyListRequests).toBe(1);

  await historyItem.getByRole('button', { name: /Подробно/i }).click();

  await expect(page.locator('#history-modal')).toHaveClass(/active/);
  await expect.poll(() => requests.historyDetailRequests).toEqual(['process-v2-1']);
  await expect(page.locator('#modal-phases')).toContainText('body end detected');
  await expect(page.locator('#modal-phases')).toContainText('Тело завершено по температуре');
  await expect(page.locator('#modal-results-grid')).toContainText('Safety timeline');
  await expect(page.locator('#modal-results-grid')).toContainText('Авария по перегреву');
  await expect(page.locator('#modal-results-grid')).toContainText('Температура вернулась в норму');
});
