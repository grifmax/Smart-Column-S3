import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

test.use({
  viewport: { width: 430, height: 932 },
});

test('mobile tools accordion can collapse all calculators and opens downward from the tapped header', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);
  await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false),
  });

  await page.goto('/index.html');
  await page.evaluate(() => {
    document.querySelectorAll('.tab[data-tab="tools"]')[0]?.click();
  });

  const toolsRoot = page.locator('#tools');
  await expect(toolsRoot).toHaveAttribute('data-tools-layout', 'mobile');

  const firstToggle = page.locator('.tools-card-mobile-toggle').first();
  await expect(firstToggle).toHaveAttribute('aria-expanded', 'true');

  await firstToggle.click();
  await expect(firstToggle).toHaveAttribute('aria-expanded', 'false');

  const heatToggle = page.locator('.tools-card-mobile-toggle', { hasText: /Нагрев/i });
  const heatCard = page.locator('.tools-card').filter({ has: heatToggle });

  await heatToggle.click();
  await expect(heatToggle).toHaveAttribute('aria-expanded', 'true');
  await expect(heatCard.locator('.tools-card-body')).toBeVisible();

  await page.waitForTimeout(450);
  await expect.poll(() =>
    heatToggle.evaluate((element) => Math.round(element.getBoundingClientRect().top))
  ).toBeLessThan(140);
});
