import { test, expect } from '@playwright/test';
import {
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

test('charts page connects via websocket and renders chart updates', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);

  await page.goto('/charts.html');

  await expect(page.locator('#connection-text')).toHaveText('Подключено');
  await expect(page.locator('#chart-temperatures')).toHaveAttribute('data-mock-rendered', '1');

  await page.evaluate(() => {
    window.__mockWs.emit({
      t_cube: 78.2,
      t_column_bottom: 77.4,
      t_column_top: 76.9,
      t_reflux: 75.8,
      t_tsa: 44.3,
      t_water_in: 19.5,
      t_water_out: 23.2,
      p_cube: 2,
      p_atm: 1012,
      p_flood: 14,
      voltage: 229,
      current: 3.1,
      power: 710,
      energy: 0.2,
      frequency: 50,
      pf: 0.97,
      pump_speed: 1200,
      pump_volume: 50,
      abv: 91.3,
      volume_heads: 150,
      volume_body: 300,
      volume_tails: 20,
      uptime: 3661,
    });
  });

  await expect(page.locator('#uptime')).toHaveText('01:01:01');

  const chartSummary = await page.evaluate(() => window.__mockApexCharts.getSummary());
  const temperaturesChart = chartSummary.find((chart) => chart.id === 'chart-temperatures');
  const pumpChart = chartSummary.find((chart) => chart.id === 'chart-pump');

  expect(temperaturesChart?.rendered).toBe(true);
  expect(temperaturesChart?.lastSeries?.[0]?.points).toBe(1);
  expect(pumpChart?.lastSeries?.[0]?.points).toBe(1);
});
