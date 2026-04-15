import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  buildStirrerSettingsPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

test('stirrer widget and equipment settings stay in sync', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);

  let stirrerState = {
    running: false,
    speed: 0,
    available: true,
    autoMode: false,
  };
  let stirrerSettingsState = {
    enabled: true,
    defaultSpeedPercent: 50,
    autoMashing: true,
    autoFermentation: false,
    autoNbk: false,
  };

  const requests = await installCommonApiMocks(page, {
    statusPayload: () => buildStatusPayload(0, false, {
      stirrer: { ...stirrerState },
    }),
    stirrerSettingsPayload: () => buildStirrerSettingsPayload({
      ...stirrerSettingsState,
      ...stirrerState,
    }),
    stirrerSettingsSaveResponse: ({ postData }) => {
      stirrerSettingsState = {
        ...stirrerSettingsState,
        ...(postData || {}),
      };
      return {
        success: true,
        settings: buildStirrerSettingsPayload(stirrerSettingsState),
        stirrer: { ...stirrerState },
      };
    },
    stirrerActionResponse: ({ pathname, postData }) => {
      if (pathname === '/api/stirrer/start') {
        stirrerState = {
          ...stirrerState,
          running: true,
          speed: Number(postData?.speed || stirrerSettingsState.defaultSpeedPercent),
          autoMode: false,
        };
      } else if (pathname === '/api/stirrer/set') {
        stirrerState = {
          ...stirrerState,
          running: true,
          speed: Number(postData?.speed || stirrerState.speed || stirrerSettingsState.defaultSpeedPercent),
          autoMode: false,
        };
      } else if (pathname === '/api/stirrer/stop') {
        stirrerState = {
          ...stirrerState,
          running: false,
          speed: 0,
          autoMode: false,
        };
      }

      return {
        success: true,
        message: 'ok',
        stirrer: { ...stirrerState },
      };
    },
  });

  await page.goto('/index.html');

  await expect(page.locator('#monitor-stirrer-badge')).toContainText('Готова');
  await expect(page.locator('#monitor-stirrer-availability')).toContainText('MCP4725 OK');

  await page.getByRole('button', { name: '75%' }).click();
  await expect(page.locator('#monitor-stirrer-speed-input')).toHaveValue('75');
  await page.locator('#monitor-stirrer-start').click();

  await expect(page.locator('#monitor-stirrer-badge')).toContainText('Ручной');
  await expect(page.locator('#monitor-stirrer-speed')).toContainText('75');
  await expect(page.locator('#monitor-stirrer-mode')).toContainText('Ручной');

  await page.locator('#monitor-stirrer-speed-input').fill('60');
  await page.locator('#monitor-stirrer-apply').click();
  await expect(page.locator('#monitor-stirrer-speed')).toContainText('60');

  await page.locator('#monitor-stirrer-stop').click();
  await expect(page.locator('#monitor-stirrer-badge')).toContainText('Готова');
  await expect(page.locator('#monitor-stirrer-speed')).toContainText('0');

  await expect.poll(() => requests.stirrerActions.filter((entry) => entry.pathname === '/api/stirrer/start').length).toBe(1);
  await expect.poll(() => requests.stirrerActions.filter((entry) => entry.pathname === '/api/stirrer/set').length).toBe(1);
  await expect.poll(() => requests.stirrerActions.filter((entry) => entry.pathname === '/api/stirrer/stop').length).toBe(1);

  await page.evaluate(() => {
    document.querySelectorAll('.tab[data-tab="equipment"]')[0]?.click();
  });

  await expect(page.locator('#equipment')).toHaveClass(/active/);
  await page.locator('.equipment-testing-nav-item[data-equipment-workbench-card-id="stirrer-settings"]').click();

  await expect(page.locator('#stirrer-enabled')).toBeVisible();
  await expect(page.locator('#stirrer-default-speed')).toHaveValue('50');

  await page.locator('#stirrer-default-speed').fill('55');
  await page.locator('#stirrer-auto-nbk').check();
  await page.locator('#stirrer-auto-fermentation').check();
  await page.getByRole('button', { name: 'Сохранить мешалку' }).click();

  await expect.poll(() => requests.stirrerSettingsSaves.length).toBe(1);
  await expect.poll(() => requests.stirrerSettingsSaves[0]?.defaultSpeedPercent).toBe(55);
  await expect.poll(() => requests.stirrerSettingsSaves[0]?.autoNbk).toBeTruthy();
  await expect.poll(() => requests.stirrerSettingsSaves[0]?.autoFermentation).toBeTruthy();
  await expect(page.locator('#stirrer-settings-state')).toContainText('MCP4725 OK');
});
