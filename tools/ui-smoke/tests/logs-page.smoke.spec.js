import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

test('logs page shows system events and can clear the journal', async ({ page }) => {
  const assetStatuses = [];
  const requests = await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false, { uptime: 321 }),
    logEvents: [
      { seq: 1, timestamp: 5000, levelStr: 'success', message: 'Контроллер готов' },
      { seq: 2, timestamp: 12000, levelStr: 'info', message: 'Запущен demo mode' },
    ],
  });
  await installMockWebSocket(page);
  page.on('dialog', async (dialog) => dialog.accept());
  page.on('response', (response) => {
    if (response.url().includes('column-animation.css')) {
      assetStatuses.push(response.status());
    }
  });

  await page.goto('/logs.html');

  await expect(page.locator('#connection-text')).toHaveText('Подключено');
  await expect(page.locator('#uptime')).toHaveText('0:05:21');
  await expect(page.locator('#log-container')).toContainText('Контроллер готов');
  await expect(page.locator('#log-container')).toContainText('Запущен demo mode');
  await expect(page.locator('#log-container')).not.toContainText('Ошибка загрузки версий');
  expect(assetStatuses).toEqual([]);

  await page.getByRole('button', { name: /Очистить/i }).click();

  await expect.poll(() => requests.clearLogs).toBe(1);
  await expect(page.locator('#log-container')).toContainText('Журнал очищен');
});
