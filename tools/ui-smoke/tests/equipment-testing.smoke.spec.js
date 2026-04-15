import { test, expect } from '@playwright/test';
import {
  buildStatusPayload,
  installCommonApiMocks,
  installMockApexCharts,
  installMockWebSocket,
} from './helpers/smoke-helpers.js';

function buildTestingStatus(overrides = {}) {
  const base = {
    success: true,
    mode: 'idle',
    processActive: false,
    testingAllowed: true,
    availabilityReason: '',
    demoMode: false,
    physicalActuationAllowed: true,
    alarmActive: false,
    alarmMessage: '',
    safetyOk: true,
    safetyLatched: false,
    recentActions: [],
    activeTests: {
      pump: false,
      stirrer: false,
      heater: false,
      waterValve: false,
      headsValve: false,
      unoValve: false,
      servoMoving: false,
    },
    pump: {
      running: false,
      speedMlH: 0,
      targetSpeedMlH: 0,
      appliedSpeedMlH: 0,
      totalVolumeMl: 0,
      totalSteps: 0,
      taskAlive: true,
      mutexReady: true,
      lockTimeoutCount: 0,
      taskLoopCount: 42,
      cooperativeSleepCount: 2,
      fastYieldCount: 10,
    },
    stirrer: {
      enabled: true,
      available: true,
      running: false,
      speed: 0,
      autoMode: false,
      defaultSpeedPercent: 50,
    },
    heater: {
      active: false,
      powerPercent: 0,
      powerSetPercent: 0,
      minSubmergeLiters: 7.5,
    },
    valves: {
      water: false,
      heads: false,
      uno: false,
      startStopDuty: false,
      waterPulse: { active: false, remainingMs: 0 },
      headsPulse: { active: false, remainingMs: 0 },
      unoPulse: { active: false, remainingMs: 0 },
    },
    servo: {
      enabled: true,
      available: true,
      moving: false,
      fraction: 'body',
      fractionLabel: 'Тело',
      angle: 72,
      presets: [
        { index: 0, token: 'heads', label: 'Головы', enabled: true, angle: 0 },
        { index: 1, token: 'subheads', label: 'Подголовники', enabled: false, angle: 36 },
        { index: 2, token: 'body', label: 'Тело', enabled: true, angle: 72 },
        { index: 3, token: 'pretails', label: 'Предхвостье', enabled: false, angle: 108 },
        { index: 4, token: 'tails', label: 'Хвосты', enabled: true, angle: 144 },
      ],
    },
    temperatures: [
      { index: 0, label: 'Куб', valid: true, value: 78.4 },
      { index: 1, label: 'Царга низ', valid: true, value: 77.1 },
      { index: 2, label: 'Царга верх', valid: false, value: 0 },
    ],
    pressure: {
      cubeMmHg: 2.3,
      atmosphere: 1012.8,
      ok: true,
      lastUpdate: 1710500000,
    },
    hydrometer: {
      pressure: 12.2,
      density: 0.812,
      abv: 91.4,
      temperature: 21.3,
      valid: true,
      ok: true,
      lastUpdate: 1710500000,
    },
    power: {
      voltage: 229.8,
      current: 3.15,
      power: 720,
      energy: 0.231,
      frequency: 50,
      powerFactor: 0.98,
    },
  };

  return {
    ...base,
    ...overrides,
    activeTests: {
      ...base.activeTests,
      ...(overrides.activeTests || {}),
    },
    pump: {
      ...base.pump,
      ...(overrides.pump || {}),
    },
    stirrer: {
      ...base.stirrer,
      ...(overrides.stirrer || {}),
    },
    heater: {
      ...base.heater,
      ...(overrides.heater || {}),
    },
    valves: {
      ...base.valves,
      ...(overrides.valves || {}),
    },
    servo: {
      ...base.servo,
      ...(overrides.servo || {}),
    },
    pressure: {
      ...base.pressure,
      ...(overrides.pressure || {}),
    },
    hydrometer: {
      ...base.hydrometer,
      ...(overrides.hydrometer || {}),
    },
    power: {
      ...base.power,
      ...(overrides.power || {}),
    },
    recentActions: overrides.recentActions || base.recentActions,
  };
}

function prependRecentAction(currentState, action) {
  return [action, ...(currentState.recentActions || [])].slice(0, 6);
}

test('equipment testing workspace renders and sends service actions', async ({ page }) => {
  await installMockWebSocket(page);
  await installMockApexCharts(page);

  let testingState = buildTestingStatus();

  const requests = await installCommonApiMocks(page, {
    statusPayload: buildStatusPayload(0, false),
    testingStatusPayload: () => testingState,
    testingActionResponse: ({ pathname, postData }) => {
      if (pathname === '/api/testing/pump') {
        if (postData?.action === 'start') {
          const speed = Number(postData.speedMlH || 0);
          testingState = buildTestingStatus({
            activeTests: { pump: true },
            pump: {
              running: true,
              speedMlH: speed,
              targetSpeedMlH: speed,
              appliedSpeedMlH: speed,
              totalVolumeMl: 12.4,
            },
            recentActions: prependRecentAction(testingState, {
              tone: 'success',
              title: 'Тест насоса',
              detail: `Насос запущен вручную со скоростью ${speed} мл/ч.`,
            }),
          });
        } else {
          testingState = buildTestingStatus({
            recentActions: prependRecentAction(testingState, {
              tone: 'warning',
              title: 'Тест насоса',
              detail: 'Насос остановлен из сервисного экрана.',
            }),
          });
        }
        return testingState;
      }

      if (pathname === '/api/testing/stirrer') {
        if (postData?.action === 'start') {
          const speed = Number(postData.speedPercent || 0);
          testingState = buildTestingStatus({
            activeTests: { stirrer: true },
            stirrer: {
              running: true,
              speed,
              autoMode: false,
            },
            recentActions: prependRecentAction(testingState, {
              tone: 'success',
              title: 'Тест мешалки',
              detail: `Мешалка запущена вручную на ${speed}%.`,
            }),
          });
        } else if (postData?.action === 'set') {
          const speed = Number(postData.speedPercent || 0);
          testingState = buildTestingStatus({
            activeTests: { stirrer: true },
            stirrer: {
              running: true,
              speed,
              autoMode: false,
            },
            recentActions: prependRecentAction(testingState, {
              tone: 'info',
              title: 'Тест мешалки',
              detail: `Скорость мешалки изменена до ${speed}%.`,
            }),
          });
        } else {
          testingState = buildTestingStatus({
            recentActions: prependRecentAction(testingState, {
              tone: 'warning',
              title: 'Тест мешалки',
              detail: 'Мешалка остановлена из сервисного экрана.',
            }),
          });
        }
        return testingState;
      }

      if (pathname === '/api/testing/valves' && postData?.action === 'pulse') {
        const target = postData.target;
        const durationMs = Number(postData.durationMs || 0);
        testingState = buildTestingStatus({
          activeTests: {
            [`${target}Valve`]: true,
          },
          valves: {
            [`${target}`]: true,
            [`${target}Pulse`]: { active: true, remainingMs: durationMs },
          },
          recentActions: prependRecentAction(testingState, {
            tone: 'info',
            title: target === 'water' ? 'Клапан воды' : target === 'heads' ? 'Клапан голов' : 'УНО',
            detail: `Импульс ${durationMs} мс.`,
          }),
        });
        return testingState;
      }

      if (pathname === '/api/testing/servo' && postData?.action === 'preset') {
        testingState = buildTestingStatus({
          activeTests: { servoMoving: true },
          servo: {
            moving: true,
            fraction: postData.preset,
            fractionLabel: postData.preset === 'heads' ? 'Головы' : 'Тело',
            angle: postData.preset === 'heads' ? 0 : 72,
          },
          recentActions: prependRecentAction(testingState, {
            tone: 'success',
            title: 'Сервопривод',
            detail: `Сервопривод переведён в позицию ${postData.preset}.`,
          }),
        });
        return testingState;
      }

      if (pathname === '/api/testing/stop-all') {
        testingState = buildTestingStatus({
          recentActions: prependRecentAction(testingState, {
            tone: 'warning',
            title: 'Остановить все тесты',
            detail: 'Все ручные сервисные воздействия остановлены одной командой.',
          }),
        });
        return testingState;
      }

      return testingState;
    },
  });

  await page.goto('/index.html');
  await page.evaluate(() => {
    document.querySelectorAll('.tab[data-tab="equipment"]')[0]?.click();
  });

  await expect(page.locator('#equipment')).toHaveClass(/active/);
  await expect(page.locator('#pump-ml-per-rev')).toBeVisible();

  await page.locator('[data-equipment-section-btn="calibration"]').click();
  await expect(page.locator('#cal-speed')).toBeVisible();
  await page.locator('.equipment-testing-nav-item[data-equipment-workbench-card-id="temp-calibration"]').click();
  await expect(page.locator('#sensorList')).toBeVisible();

  await page.locator('[data-equipment-section-btn="testing"]').click();

  await expect(page.locator('#equipment-test-allow-badge')).toContainText('Доступно');
  await expect(page.locator('#equipment-test-active-summary')).toContainText('Ничего не включено');
  await expect(page.locator('#equipment-testing-temps-list')).toContainText('Куб');
  await expect.poll(() => requests.testingStatusRequests).toBeGreaterThan(0);

  await page.getByRole('button', { name: '1500' }).click();
  await expect(page.locator('#equipment-test-pump-speed')).toHaveValue('1500');
  await page.locator('#equipment-test-pump-toggle').click();

  await expect(page.locator('#equipment-test-pump-badge')).toContainText('Работает');
  await expect(page.locator('#equipment-test-pump-applied')).toContainText('1500');
  await expect(page.locator('#equipment-test-active-summary')).toContainText('Насос');
  await expect(page.locator('#equipment-test-action-journal')).toContainText('Тест насоса');
  await expect(page.locator('#equipment-test-action-journal')).toContainText('1500 мл/ч');
  await expect.poll(() =>
    requests.testingActions.some((entry) =>
      entry.pathname === '/api/testing/pump' &&
      entry.body?.action === 'start' &&
      Number(entry.body?.speedMlH) === 1500,
    ),
  ).toBeTruthy();

  await page.locator('.equipment-testing-nav-item[data-testing-card-id="stirrer"]').click();
  await expect(page.locator('#equipment-test-stirrer-start')).toBeVisible();
  await page.getByRole('button', { name: '75%' }).click();
  await expect(page.locator('#equipment-test-stirrer-speed')).toHaveValue('75');
  await page.locator('#equipment-test-stirrer-start').click();
  await expect(page.locator('#equipment-test-stirrer-badge')).toContainText('Работает');
  await expect(page.locator('#equipment-test-stirrer-speed-live')).toContainText('75');
  await expect(page.locator('#equipment-test-active-summary')).toContainText('Мешалка');
  await page.locator('#equipment-test-stirrer-speed').fill('60');
  await page.locator('#equipment-test-stirrer-apply').click();
  await expect(page.locator('#equipment-test-stirrer-speed-live')).toContainText('60');
  await expect(page.locator('#equipment-test-action-journal')).toContainText('Скорость мешалки изменена до 60%');
  await expect.poll(() =>
    requests.testingActions.some((entry) =>
      entry.pathname === '/api/testing/stirrer' &&
      entry.body?.action === 'set' &&
      Number(entry.body?.speedPercent) === 60,
    ),
  ).toBeTruthy();

  await page.locator('.equipment-testing-nav-item[data-testing-card-id="servo"]').click();
  await expect(page.locator('[data-servo-preset="heads"]')).toBeVisible();
  await page.locator('[data-servo-preset="heads"]').click();
  await expect(page.locator('#equipment-test-servo-badge')).toContainText('Движение');
  await expect(page.locator('#equipment-test-servo-fraction')).toContainText('Головы');

  await page.locator('.equipment-testing-nav-item[data-testing-card-id="valves"]').click();
  await expect(page.locator('#equipment-test-water-pulse')).toBeVisible();
  await page.locator('#equipment-test-valve-pulse-duration').fill('1800');
  await page.locator('#equipment-test-water-pulse').click();
  await expect(page.locator('#equipment-test-water-toggle-badge')).toContainText('Импульс');
  await expect(page.locator('#equipment-test-water-toggle-pulse-hint')).toContainText('1.8');
  await expect(page.locator('#equipment-test-action-journal')).toContainText('Клапан воды');
  await expect.poll(() =>
    requests.testingActions.some((entry) =>
      entry.pathname === '/api/testing/valves' &&
      entry.body?.target === 'water' &&
      entry.body?.action === 'pulse' &&
      Number(entry.body?.durationMs) === 1800,
    ),
  ).toBeTruthy();

  await page.locator('#equipment-test-stop-all').click();
  await expect(page.locator('#equipment-test-pump-badge')).toContainText('Ожидание');
  await expect(page.locator('#equipment-test-active-summary')).toContainText('Ничего не включено');
  await expect.poll(() =>
    requests.testingActions.some((entry) => entry.pathname === '/api/testing/stop-all'),
  ).toBeTruthy();

  await page.locator('[data-equipment-section-btn="calibration"]').click();
  await page.locator('.equipment-testing-nav-item[data-equipment-workbench-card-id="pump-calibration"]').click();
  await expect(page.locator('#pumpCurrent')).toBeVisible();
});
