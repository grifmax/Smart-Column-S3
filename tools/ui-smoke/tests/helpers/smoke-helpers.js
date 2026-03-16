export function buildStatusPayload(mode = 0, paused = false, overrides = {}) {
  const modeMap = {
    0: 'idle',
    1: 'rectification',
    2: 'distillation',
    3: 'manual',
    4: 'mashing',
    5: 'hold',
    6: 'nbk',
    7: 'fermentation',
  };

  const basePayload = {
    mode,
    modeStr: modeMap[mode] || 'idle',
    phase: 0,
    phaseStr: 'idle',
    paused,
    demoMode: false,
    safetyOk: true,
    uptime: 120,
    temps: {
      cube: 78.4,
      columnBottom: 77.1,
      columnTop: 76.8,
      reflux: 75.2,
      deflegmator: 23.0,
      product: 23.0,
      tsa: 45.0,
      waterIn: 20.0,
      waterOut: 24.0,
    },
    pressure: { cube: 0, atm: 1013, kpa: 101.3 },
    power: { voltage: 230, current: 3.2, power: 736, energy: 0.1, frequency: 50, pf: 0.98 },
    pump: { speedMlH: 0, totalMl: 0, running: false },
    hydrometer: { abv: 0, density: 0, valid: false },
    volumes: { heads: 0, body: 0, tails: 0 },
    equipment: { heaterPowerW: 3000, columnHeightMm: 1500 },
  };

  return {
    ...basePayload,
    ...overrides,
    temps: {
      ...basePayload.temps,
      ...(overrides.temps || {}),
    },
    pressure: {
      ...basePayload.pressure,
      ...(overrides.pressure || {}),
    },
    power: {
      ...basePayload.power,
      ...(overrides.power || {}),
    },
    pump: {
      ...basePayload.pump,
      ...(overrides.pump || {}),
    },
    hydrometer: {
      ...basePayload.hydrometer,
      ...(overrides.hydrometer || {}),
    },
    volumes: {
      ...basePayload.volumes,
      ...(overrides.volumes || {}),
    },
    equipment: {
      ...basePayload.equipment,
      ...(overrides.equipment || {}),
    },
  };
}

export function buildVersionPayload(overrides = {}) {
  const basePayload = {
    firmware: {
      version: 'test-smoke',
      buildDate: 'Mar 13 2026',
      buildTime: '12:00:00',
    },
    frontend: {
      buildDate: 'Mar 13 2026',
      buildTime: '12:00:00',
    },
  };

  return {
    ...basePayload,
    ...overrides,
    firmware: {
      ...basePayload.firmware,
      ...(overrides.firmware || {}),
    },
    frontend: {
      ...basePayload.frontend,
      ...(overrides.frontend || {}),
    },
  };
}

export async function installMockWebSocket(page) {
  await page.addInitScript(() => {
    const sockets = [];

    class MockWebSocket {
      static CONNECTING = 0;
      static OPEN = 1;
      static CLOSING = 2;
      static CLOSED = 3;

      constructor(url) {
        this.url = url;
        this.readyState = MockWebSocket.CONNECTING;
        this.sent = [];
        this.onopen = null;
        this.onmessage = null;
        this.onerror = null;
        this.onclose = null;
        this._listeners = new Map();

        sockets.push(this);

        window.setTimeout(() => {
          if (this.readyState !== MockWebSocket.CONNECTING) {
            return;
          }

          this.readyState = MockWebSocket.OPEN;
          this._emit('open', { type: 'open', target: this });
        }, 0);
      }

      addEventListener(type, listener) {
        const listeners = this._listeners.get(type) || [];
        listeners.push(listener);
        this._listeners.set(type, listeners);
      }

      removeEventListener(type, listener) {
        const listeners = this._listeners.get(type) || [];
        this._listeners.set(
          type,
          listeners.filter((candidate) => candidate !== listener)
        );
      }

      send(payload) {
        this.sent.push(payload);
      }

      close(code = 1000, reason = 'closed') {
        if (this.readyState === MockWebSocket.CLOSED) {
          return;
        }

        this.readyState = MockWebSocket.CLOSED;
        this._emit('close', {
          type: 'close',
          code,
          reason,
          wasClean: true,
          target: this,
        });
      }

      __emitMessage(payload) {
        const data = typeof payload === 'string' ? payload : JSON.stringify(payload);
        this._emit('message', { type: 'message', data, target: this });
      }

      __emitError(message = 'Mock WebSocket error') {
        this._emit('error', {
          type: 'error',
          message,
          error: new Error(message),
          target: this,
        });
      }

      _emit(type, event) {
        const handler = this[`on${type}`];
        if (typeof handler === 'function') {
          handler.call(this, event);
        }

        const listeners = this._listeners.get(type) || [];
        for (const listener of listeners) {
          listener.call(this, event);
        }
      }
    }

    function getLastSocket() {
      return sockets[sockets.length - 1] || null;
    }

    window.WebSocket = MockWebSocket;
    window.__mockWs = {
      emit(payload) {
        const socket = getLastSocket();
        if (socket) {
          socket.__emitMessage(payload);
        }
      },
      close(code = 1000, reason = 'closed') {
        const socket = getLastSocket();
        if (socket) {
          socket.close(code, reason);
        }
      },
      fail(message = 'Mock WebSocket error') {
        const socket = getLastSocket();
        if (socket) {
          socket.__emitError(message);
        }
      },
      getState() {
        const socket = getLastSocket();
        return {
          count: sockets.length,
          url: socket ? socket.url : null,
          readyState: socket ? socket.readyState : null,
          sent: socket ? [...socket.sent] : [],
        };
      },
    };
  });
}

export async function installMockApexCharts(page) {
  await page.route('**/npm/apexcharts**', (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/javascript',
      body: 'window.__apexChartsScriptIntercepted = true;',
    }));

  await page.addInitScript(() => {
    function mergeDeep(target, source) {
      if (!source || typeof source !== 'object') {
        return target;
      }

      const output = Array.isArray(target) ? [...target] : { ...(target || {}) };
      Object.entries(source).forEach(([key, value]) => {
        if (Array.isArray(value)) {
          output[key] = value.map((entry) => (
            entry && typeof entry === 'object'
              ? mergeDeep({}, entry)
              : entry
          ));
          return;
        }

        if (value && typeof value === 'object') {
          output[key] = mergeDeep(output[key], value);
          return;
        }

        output[key] = value;
      });

      return output;
    }

    const registry = [];

    class MockApexCharts {
      constructor(element, options = {}) {
        this.element = element;
        this.w = {
          config: mergeDeep({}, options),
        };
        this.renderCount = 0;
        this.hiddenSeries = [];
        this.lastUpdateOptions = null;
        this.lastSeries = [];
        registry.push(this);
      }

      render() {
        this.renderCount += 1;
        if (this.element) {
          this.element.dataset.mockChart = '1';
          this.element.dataset.mockRendered = '1';
        }
        return Promise.resolve();
      }

      updateSeries(series = []) {
        const existingSeries = Array.isArray(this.w.config.series) ? this.w.config.series : [];
        const normalizedSeries = series.map((entry, index) => ({
          ...existingSeries[index],
          ...entry,
          name: entry?.name ?? existingSeries[index]?.name ?? `Series ${index + 1}`,
        }));

        this.w.config.series = normalizedSeries;
        this.lastSeries = normalizedSeries.map((entry) => ({
          name: entry.name,
          points: Array.isArray(entry.data) ? entry.data.length : 0,
        }));

        if (this.element) {
          this.element.dataset.lastSeries = JSON.stringify(this.lastSeries);
        }

        return Promise.resolve();
      }

      updateOptions(options = {}) {
        this.lastUpdateOptions = options;
        this.w.config = mergeDeep(this.w.config, options);
        return Promise.resolve();
      }

      showSeries(name) {
        this.hiddenSeries = this.hiddenSeries.filter((entry) => entry !== name);
        return Promise.resolve();
      }

      hideSeries(name) {
        if (!this.hiddenSeries.includes(name)) {
          this.hiddenSeries.push(name);
        }
        return Promise.resolve();
      }

      dataURI() {
        return Promise.resolve({
          imgURI: 'data:image/png;base64,iVBORw0KGgo=',
        });
      }
    }

    window.ApexCharts = MockApexCharts;
    window.__mockApexCharts = {
      getSummary() {
        return registry.map((instance) => ({
          id: instance.element?.id || null,
          rendered: instance.renderCount > 0,
          hiddenSeries: [...instance.hiddenSeries],
          lastSeries: instance.lastSeries,
        }));
      },
    };
  });
}

export async function installCommonApiMocks(page, options = {}) {
  const requests = {
    clearLogs: 0,
    exportRequests: 0,
    historyDetailRequests: [],
    historyListRequests: 0,
    logQueries: [],
    processStarts: [],
    statusRequests: 0,
    testingActions: [],
    testingStatusRequests: 0,
  };

  const versionPayload = options.versionPayload || buildVersionPayload();
  const statusPayload = options.statusPayload || buildStatusPayload();
  const historyListPayload = options.historyListPayload || { processes: [] };
  const historyDetailsPayloads = options.historyDetailsPayloads || {};
  const logEvents = options.logEvents || [];
  const processStartResponse = options.processStartResponse || { success: true };
  const calibrationPayload = options.calibrationPayload || {
    pump: {
      mlPerRev: 0.12,
      stepsPerRev: 200,
      microsteps: 32,
    },
    temperatures: [],
  };
  const testingStatusPayload = options.testingStatusPayload || null;
  const testingActionResponse = options.testingActionResponse || null;

  await page.route('**/api/**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const { pathname, searchParams } = url;
    const method = request.method().toUpperCase();

    if (pathname === '/api/web/user') {
      await route.fulfill({ status: 404, body: 'not found' });
      return;
    }

    if (pathname === '/api/version') {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(versionPayload),
      });
      return;
    }

    if (pathname === '/api/status') {
      requests.statusRequests += 1;
      const payload = typeof statusPayload === 'function'
        ? statusPayload({ pathname, searchParams, method, requests })
        : statusPayload;

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname === '/api/calibration' && method === 'GET') {
      const payload = typeof calibrationPayload === 'function'
        ? calibrationPayload({ pathname, searchParams, method, requests })
        : calibrationPayload;

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname === '/api/testing/status' && method === 'GET') {
      requests.testingStatusRequests += 1;
      const payload = typeof testingStatusPayload === 'function'
        ? testingStatusPayload({ pathname, searchParams, method, requests })
        : (testingStatusPayload || {});

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname.startsWith('/api/testing/') && method === 'POST') {
      let postData = null;
      try {
        postData = request.postDataJSON();
      } catch {
        postData = request.postData() || null;
      }

      requests.testingActions.push({
        pathname,
        body: postData,
      });

      const payload = typeof testingActionResponse === 'function'
        ? testingActionResponse({ pathname, searchParams, method, requests, postData })
        : (testingActionResponse || { success: true });

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname === '/api/history' && method === 'GET') {
      requests.historyListRequests += 1;
      const payload = typeof historyListPayload === 'function'
        ? historyListPayload({ pathname, searchParams, method, requests })
        : historyListPayload;

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname.startsWith('/api/history/') && !pathname.endsWith('/export') && method === 'GET') {
      const historyId = decodeURIComponent(pathname.slice('/api/history/'.length));
      requests.historyDetailRequests.push(historyId);
      const payload = typeof historyDetailsPayloads === 'function'
        ? historyDetailsPayloads({ historyId, pathname, searchParams, method, requests })
        : historyDetailsPayloads[historyId];

      if (!payload) {
        await route.fulfill({
          status: 404,
          contentType: 'application/json',
          body: JSON.stringify({ error: `Missing history fixture for ${historyId}` }),
        });
        return;
      }

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(payload),
      });
      return;
    }

    if (pathname === '/api/logs/events') {
      requests.logQueries.push(url.search);
      const sourceEvents = typeof logEvents === 'function'
        ? logEvents({ pathname, searchParams, method, requests })
        : logEvents;

      let events = Array.isArray(sourceEvents) ? [...sourceEvents] : [];
      const since = Number(searchParams.get('since') || 0);
      const limit = Number(searchParams.get('limit') || 0);

      if (since > 0) {
        events = events.filter((event) => Number(event.seq || 0) > since);
      }

      if (limit > 0) {
        events = events.slice(-limit);
      }

      const nextSeq = events.reduce((max, event) => Math.max(max, Number(event.seq || 0)), 0);

      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ events, nextSeq }),
      });
      return;
    }

    if (pathname === '/api/logs/events/clear' && method === 'POST') {
      requests.clearLogs += 1;
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ success: true }),
      });
      return;
    }

    if (pathname === '/api/export') {
      requests.exportRequests += 1;
      await route.fulfill({
        status: 200,
        contentType: 'text/csv',
        body: 'timestamp,message\n00:00:01,export\n',
      });
      return;
    }

    if (pathname === '/api/process/start' && method === 'POST') {
      let postData = null;
      try {
        postData = request.postDataJSON();
      } catch {
        postData = request.postData() || null;
      }

      requests.processStarts.push(postData);
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(processStartResponse),
      });
      return;
    }

    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: '{}',
    });
  });

  return requests;
}
