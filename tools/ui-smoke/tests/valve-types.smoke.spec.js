import { test, expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import {
  buildEquipmentSettingsPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

const repoRoot = new URL('../../../', import.meta.url);
const channels = ['water', 'heads', 'body', 'tails', 'uno', 'startStop'];

// Exercise current sources without rebuilding or overwriting generated data/app.js.
async function installSourceUi(page) {
  await page.addInitScript(() => {
    window.__APP_VERSION__ = 'valve-types-source-test';
    document.addEventListener('DOMContentLoaded', () => {
      window.__originalValveSelects = [...document.querySelectorAll('select[id^="valve-type-"]')];
    }, { once: true });
  });
  await page.route('**/index.html', async (route) => {
    const html = await readFile(new URL('data/index.html', repoRoot), 'utf8');
    expect(html).toMatch(/<script src="app\.js[^\"]*"><\/script>/);
    await route.fulfill({
      contentType: 'text/html',
      body: html.replace(/<script src="app\.js[^\"]*"><\/script>/,
        '<script type="module" src="/src/web/main.js"></script>'),
    });
  });
  await page.route('**/src/web/**/*.js', async (route) => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname.includes('..')) throw new Error('Invalid module path');
    await route.fulfill({
      contentType: 'application/javascript',
      body: await readFile(fileURLToPath(new URL(`.${pathname}`, repoRoot))),
    });
  });
}

for (const mobile of [false, true]) {
  test(`valve types are discoverable and preserve equipment cards (${mobile ? 'mobile' : 'desktop'})`, async ({ page }, testInfo) => {
    const uiErrors = [];
    page.on('pageerror', (error) => uiErrors.push(error.message));
    await page.setViewportSize(mobile ? { width: 390, height: 844 } : { width: 1440, height: 1000 });
    await installSourceUi(page);
    await installMockWebSocket(page);
    await installMockApexCharts(page);
    const initialTypes = { water: 'nc', heads: 'no', body: 'nc', tails: 'no', uno: 'nc', startStop: 'no' };
    const requests = await installCommonApiMocks(page, {
      equipmentSettingsPayload: buildEquipmentSettingsPayload({
        valveNormallyOpenMask: 42,
        ...(mobile ? { valveTypes: initialTypes } : {}),
        coolingPwmEnabled: true,
      }),
    });
    await page.goto('/index.html');
    await page.evaluate(() => document.querySelector('.tab[data-tab="equipment"]').click());
    const pane = page.locator('[data-equipment-section-pane="parameters"]');
    await expect(page.locator('#pump-ml-per-rev')).toBeVisible();
    await expect.poll(() => requests.equipmentSettingsRequests).toBeGreaterThan(0);

    // Use a visible user-facing entry: sidebar or responsive accordion.
    const cardButton = (id) => pane.locator(
      `.equipment-testing-nav-item[data-equipment-workbench-card-id="${id}"]:visible, .equipment-card[data-equipment-workbench-card-id="${id}"] > .equipment-test-mobile-toggle:visible`,
    ).first();
    const valvesButton = cardButton('valve-settings');
    await expect(valvesButton).toBeVisible();
    await expect(valvesButton).toContainText('Клапаны');
    await valvesButton.click();
    const valvesCard = pane.locator('.equipment-card').filter({ has: page.locator('#valve-type-water') });
    await expect(valvesCard).toContainText('Базовые узлы');
    await expect(valvesCard).toContainText('НЗ');
    await expect(valvesCard).toContainText('Ошибка в типе оставит канал в неверном положении.');
    expect(await page.evaluate(() => ({
      count: window.__originalValveSelects.length,
      preserved: window.__originalValveSelects.every((select) => select === document.getElementById(select.id)),
    }))).toEqual({ count: 6, preserved: true });
    for (const channel of channels) {
      const select = page.locator(`#valve-type-${channel}`);
      await expect(select).toHaveCount(1);
      await expect(select).toBeVisible();
      await expect(select).toHaveValue(initialTypes[channel]);
      expect(await select.locator('option').evaluateAll((options) => options.map((option) => option.value))).toEqual(['nc', 'no']);
      await select.selectOption(initialTypes[channel] === 'nc' ? 'no' : 'nc');
    }

    // Unsaved edits survive navigation in both directions; adjacent cards stay usable.
    await cardButton('pump-settings').click();
    await expect(page.locator('#pump-ml-per-rev')).toBeVisible();
    await page.locator('#pump-ml-per-rev').fill('0.16');
    await cardButton('cube-settings').click();
    await expect(page.locator('#heater-power-w')).toBeVisible();
    await expect(page.locator('#heater-power-w')).toHaveValue('3000');
    await page.locator('#heater-power-w').fill('4200');
    await cardButton('cooling-settings').click();
    await expect(page.locator('#water-autostart-cube-temp')).toBeVisible();
    await expect(page.locator('#cooling-pwm-enabled')).toBeVisible();
    await expect(page.locator('#cooling-pwm-enabled')).toBeChecked();
    await cardButton('stirrer-settings').click();
    await expect(page.locator('#stirrer-enabled')).toBeVisible();
    await cardButton('hardware-status').click();
    await expect(page.locator('#hardware-modules-list')).toBeVisible();
    await valvesButton.click();
    for (const channel of channels) {
      await expect(page.locator(`#valve-type-${channel}`)).toHaveValue(initialTypes[channel] === 'nc' ? 'no' : 'nc');
    }
    await valvesCard.screenshot({ path: testInfo.outputPath('valves-card.png') });
    await valvesCard.getByRole('button', { name: 'Сохранить параметры', exact: true }).click();
    await expect.poll(() => requests.equipmentSettingsSaves.length).toBe(1);
    expect(requests.equipmentSettingsSaves[0]).toMatchObject({
      valveTypes: { water: 'no', heads: 'nc', body: 'no', tails: 'nc', uno: 'no', startStop: 'nc' },
      heaterPowerW: 4200,
      columnHeightMm: 1500,
      cubeVolumeL: 37,
      waterAutoStartCubeTempC: 45,
      coolingPwmEnabled: true,
    });
    expect(requests.testingActions).toEqual([]);
    expect(requests.processStarts).toEqual([]);
    await cardButton('pump-settings').click();
    await expect(page.locator('#pump-ml-per-rev')).toHaveValue('0.16');
    await page.locator('[data-equipment-section-btn="calibration"]').click();
    await expect(page.locator('#cal-speed')).toBeVisible();
    expect(uiErrors).toEqual([]);
  });
}
