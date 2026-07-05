import { chromium } from '@playwright/test';

const baseUrl = process.argv[2] || 'http://192.168.3.152';

const consoleIssues = [];
const pageErrors = [];

async function fetchJson(page, path, options = {}) {
  return page.evaluate(async ({ path, options }) => {
    const response = await fetch(path, options);
    const text = await response.text();
    let body = text;
    try {
      body = JSON.parse(text);
    } catch {
      // Keep original text body for diagnostics.
    }
    return {
      ok: response.ok,
      status: response.status,
      body
    };
  }, { path, options });
}

async function exists(page, selector) {
  return (await page.locator(selector).count()) > 0;
}

function sectionSelector(id, type = 'btn') {
  return type === 'btn'
    ? `[data-settings-section-btn="${id}"]`
    : `[data-settings-section-pane="${id}"]`;
}

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage();

page.on('console', (msg) => {
  if (msg.type() !== 'error') {
    return;
  }

  const text = msg.text();
  if (text.includes('favicon') || text.includes('404')) {
    return;
  }
  consoleIssues.push(text);
});

page.on('pageerror', (error) => {
  pageErrors.push(String(error));
});

page.on('dialog', async (dialog) => {
  await dialog.accept();
});

try {
  const results = {};

  await page.goto(`${baseUrl}/index.html`, {
    waitUntil: 'domcontentloaded',
    timeout: 30000
  });
  await page.waitForTimeout(3000);

  await page.locator('.tab[data-tab="settings"]').click();
  await page.waitForSelector('.settings-shell', { timeout: 15000 });

  results.sections = await page.locator('[data-settings-section-btn]').evaluateAll((buttons) =>
    buttons.map((button) => ({
      id: button.dataset.settingsSectionBtn,
      label: button.textContent?.trim() || ''
    }))
  );

  if (!results.sections.length) {
    throw new Error('No settings sections found');
  }

  results.visibleChecks = [];
  for (const section of results.sections) {
    await page.locator(sectionSelector(section.id, 'btn')).click();
    await page.waitForTimeout(500);

    const paneState = await page.locator(sectionSelector(section.id, 'pane')).evaluate((pane) => ({
      active: pane.classList.contains('active'),
      sample: (pane.textContent || '').slice(0, 200).trim()
    }));

    results.visibleChecks.push({
      section: section.id,
      ...paneState
    });
  }

  results.values = {
    cloudEnabledPresent: await exists(page, '#cloud-enabled'),
    esp32SelectPresent: await exists(page, '#esp32-device-select'),
    mqttEnabledPresent: await exists(page, '#mqtt-enabled'),
    mqttState: (await page.locator('#mqtt-config-state').textContent())?.trim() || '',
    authEnabledPresent: await exists(page, '#auth-enabled'),
    notificationsPresent: await exists(page, '#browser-notifications-enabled'),
    demoChecked: await page.locator('#demo-mode-enabled').isChecked(),
    firmwareVersion: (await page.locator('#firmware-version').textContent())?.trim() || '',
    rebootState: (await page.locator('#reboot-settings-state').textContent())?.trim() || ''
  };

  const demoCheckbox = page.locator('#demo-mode-enabled');
  const initialDemo = await demoCheckbox.isChecked();
  const demoBefore = await fetchJson(page, '/api/settings/demo');

  await demoCheckbox.setChecked(!initialDemo);
  await page.waitForTimeout(1500);
  const demoMid = await fetchJson(page, '/api/settings/demo');

  await demoCheckbox.setChecked(initialDemo);
  await page.waitForTimeout(1500);
  const demoAfter = await fetchJson(page, '/api/settings/demo');

  results.demoRoundTrip = {
    before: demoBefore.body,
    initialDemo,
    mid: demoMid.body,
    after: demoAfter.body
  };

  results.consoleIssues = consoleIssues;
  results.pageErrors = pageErrors;

  if (consoleIssues.length || pageErrors.length) {
    throw new Error(JSON.stringify(results, null, 2));
  }

  if (!results.visibleChecks.every((item) => item.active)) {
    throw new Error(JSON.stringify(results, null, 2));
  }

  if (!results.values.cloudEnabledPresent ||
      !results.values.esp32SelectPresent ||
      !results.values.mqttEnabledPresent ||
      !results.values.mqttState ||
      !results.values.authEnabledPresent ||
      !results.values.notificationsPresent ||
      !results.values.firmwareVersion) {
    throw new Error(JSON.stringify(results, null, 2));
  }

  if (results.demoRoundTrip.mid?.demoMode !== !initialDemo ||
      results.demoRoundTrip.after?.demoMode !== initialDemo) {
    throw new Error(JSON.stringify(results, null, 2));
  }

  console.log(JSON.stringify(results, null, 2));
} finally {
  await browser.close();
}
