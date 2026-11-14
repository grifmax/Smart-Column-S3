import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

const mainTabs = [
  'monitor',
  'control',
  'profiles',
  'settings',
  'wifi',
  'equipment',
  'safety',
  'history',
  'tools',
  'ai',
];

test('main tabs stay in the shared content area with a full-width status toolbar', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);
  await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false),
  });
  await page.goto('/index.html');

  for (const tabId of mainTabs) {
    await page.locator(`.sidebar-item.tab[data-tab="${tabId}"]`).click();
    await expect(page.locator(`#${tabId}`)).toHaveClass(/active/);

    const layout = await page.evaluate((id) => {
      const contentArea = document.querySelector('.content-area');
      const toolbar = document.querySelector('.toolbar');
      const tab = document.getElementById(id);
      const areaRect = contentArea.getBoundingClientRect();
      const toolbarRect = toolbar.getBoundingClientRect();
      return {
        tabInsideContentArea: contentArea.contains(tab),
        tabInsideContentBody: contentArea.querySelector('.content-body').contains(tab),
        areaWidth: areaRect.width,
        toolbarWidth: toolbarRect.width,
        toolbarLeft: toolbarRect.left,
        areaLeft: areaRect.left,
        activeTabCount: document.querySelectorAll('.tab-content.active').length,
      };
    }, tabId);

    expect(layout.tabInsideContentArea, `${tabId} must stay inside the shared content area`).toBe(true);
    expect(layout.tabInsideContentBody, `${tabId} must stay inside the tab content body`).toBe(true);
    expect(layout.activeTabCount).toBe(1);
    expect(layout.toolbarWidth).toBeGreaterThan(0);
    expect(Math.abs(layout.toolbarWidth - layout.areaWidth)).toBeLessThanOrEqual(1);
    expect(Math.abs(layout.toolbarLeft - layout.areaLeft)).toBeLessThanOrEqual(1);
  }

  await expect(page.locator('#footer-firmware-version')).toHaveCount(0);
});
