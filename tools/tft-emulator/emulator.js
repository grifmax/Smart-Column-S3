/*
 * Local, API-free clickable representation of src/drivers/display.cpp.
 * The virtual display retains the ILI9488 aspect ratio: 480 × 320.
 */
(function () {
  "use strict";

  const tft = document.getElementById("tft");
  const tftViewport = document.getElementById("tft-viewport");
  const scenarioStatus = document.getElementById("scenario-status");
  const zoomRange = document.getElementById("zoom-range");
  const zoomValue = document.getElementById("zoom-value");

  const initialState = () => ({
    screen: "home",
    history: [],
    lang: "ru",
    theme: "dark",
    zoom: 1,
    running: false,
    paused: false,
    safety: true,
    selectedMode: "Ректификация",
    profile: "Профиль не выбран",
    preflightReady: false,
    sound: true,
    displayProfile: "BAL",
    stirrer: false,
    rectPage: 0,
    pumpStep: 0,
    output: { heater: false, pump: false, stirrer: false, valve: false },
    values: { heater: 3500, column: 1500, speed: 1200, volume: 2000, reflux: 3.5, temperature: 78.2 },
    editor: null,
    overlay: null,
    toast: ""
  });

  let state = initialState();

  const screens = {
    home: "ГЛАВНАЯ", modes: "ВЫБОР РЕЖИМА", setup: "ПОДГОТОВКА РЕЖИМА",
    profiles: "ПРОФИЛИ", preflight: "ПРЕДПУСКОВАЯ ПРОВЕРКА", monitor: "МОНИТОР",
    control: "УПРАВЛЕНИЕ", settings: "НАСТРОЙКИ", equipment: "ОБОРУДОВАНИЕ",
    rect: "РЕКТИФИКАЦИЯ", dist: "ДИСТИЛЛЯЦИЯ", calibration: "КАЛИБРОВКА",
    service: "СЕРВИС", temps: "ТЕМПЕРАТУРЫ", safety: "БЕЗОПАСНОСТЬ",
    events: "СОБЫТИЯ", params: "ПАРАМЕТРЫ ПРОЦЕССА", manual: "РУЧНОЕ УПРАВЛЕНИЕ",
    serviceTest: "ПРОВЕРКА ОБОРУДОВАНИЯ", dosing: "ДОЗИРОВАНИЕ", pump: "КАЛИБРОВКА НАСОСА"
  };

  const en = {
    "ГЛАВНАЯ": "HOME", "ВЫБОР РЕЖИМА": "MODE SELECT", "ПОДГОТОВКА РЕЖИМА": "MODE SETUP",
    "ПРОФИЛИ": "PROFILES", "ПРЕДПУСКОВАЯ ПРОВЕРКА": "PREFLIGHT", "МОНИТОР": "MONITOR",
    "УПРАВЛЕНИЕ": "CONTROL", "НАСТРОЙКИ": "SETTINGS", "ОБОРУДОВАНИЕ": "EQUIPMENT",
    "РЕКТИФИКАЦИЯ": "RECTIFICATION", "ДИСТИЛЛЯЦИЯ": "DISTILLATION", "КАЛИБРОВКА": "CALIBRATION",
    "СЕРВИС": "SERVICE", "ТЕМПЕРАТУРЫ": "TEMPERATURES", "БЕЗОПАСНОСТЬ": "SAFETY",
    "СОБЫТИЯ": "EVENTS", "ПАРАМЕТРЫ ПРОЦЕССА": "PROCESS PARAMETERS", "РУЧНОЕ УПРАВЛЕНИЕ": "MANUAL CONTROL",
    "ПРОВЕРКА ОБОРУДОВАНИЯ": "EQUIPMENT TEST", "ДОЗИРОВАНИЕ": "DOSING", "КАЛИБРОВКА НАСОСА": "PUMP CALIBRATION"
  };

  function tr(text) { return state.lang === "en" ? (en[text] || text) : text; }
  function esc(value) { return String(value).replace(/[&<>"']/g, c => ({ "&":"&amp;", "<":"&lt;", ">":"&gt;", "\"":"&quot;", "'":"&#039;" }[c])); }
  function button(label, action, tone = "", sub = "") {
    return `<button class="${tone}" data-action="${action}">${esc(label)}${sub ? `<em>${esc(sub)}</em>` : ""}</button>`;
  }
  function tile(label, value, unit, action, tone = "") {
    return `<button class="tile ${tone}" data-action="${action}"><span class="tile-label">${esc(label)}</span><strong class="tile-value">${esc(value)}${unit ? ` <small>${esc(unit)}</small>` : ""}</strong></button>`;
  }
  function list(label, value, action) {
    return `<button class="list-button" data-action="${action}"><span>${esc(label)}</span>${value ? `<strong>${esc(value)}</strong>` : ""}</button>`;
  }
  function data(label, value, action = "") {
    return action ? `<button class="data-row" data-action="${action}"><span>${esc(label)}</span><strong>${esc(value)}</strong></button>` : `<div class="data-row"><span>${esc(label)}</span><strong>${esc(value)}</strong></div>`;
  }

  function navigate(screen, remember = true) {
    if (screen === state.screen) return render();
    if (remember && state.screen) state.history.push(state.screen);
    state.screen = screen;
    state.toast = "";
    render();
  }
  function goBack() {
    state.screen = state.history.pop() || (state.running ? "monitor" : "home");
    render();
  }
  function goHome() { state.history = []; state.screen = state.running ? "monitor" : "home"; render(); }

  function title(showBack = false) {
    const screenTitle = tr(screens[state.screen] || "ГЛАВНАЯ");
    return `<div class="screen-title"><span>${esc(screenTitle)}</span>${showBack ? `<button class="back" data-action="back">‹ НАЗАД</button>` : ""}</div>`;
  }
  function header() {
    const mode = state.running ? state.selectedMode : "IDLE";
    return `<header class="top-bar"><span class="mode-pill">${esc(mode.toUpperCase())}</span><span class="safe-pill ${state.safety ? "safe" : "alarm"}">${state.safety ? "SAFE" : "ALARM"}</span><span class="wifi">Wi‑Fi 192.168.4.1</span></header>`;
  }
  function footer() {
    const active = state.screen;
    const tab = (label, route, own) => `<button class="${own.includes(active) ? "active" : ""}" data-action="nav:${route}">${label}</button>`;
    return `<nav class="screen-tabs">${tab(state.running ? "МОНИТОР" : "ГЛАВНАЯ", state.running ? "monitor" : "home", ["home", "modes", "setup", "profiles", "preflight", "monitor", "params", "events", "safety"])}${tab("УПРАВЛ.", "control", ["control", "manual"])}${tab("НАСТРОЙКИ", "settings", ["settings", "equipment", "rect", "dist", "calibration", "pump"])}${tab("СЕРВИС", "service", ["service", "temps", "serviceTest", "dosing"])}</nav>`;
  }

  function renderHome() {
    const process = state.running ? (state.paused ? "ПАУЗА" : "В РАБОТЕ") : "ОЖИДАНИЕ";
    return `${title()}<div class="status-card"><div class="status-main"><span>${state.running ? state.selectedMode : "Smart Column S3"}</span><strong>${process}</strong></div><div class="screen-note">${state.running ? "Фаза: отбор тела · 00:18 / 00:45" : "Выберите режим для подготовки процесса"}</div><div class="progress"><i style="width:${state.running ? "42" : "0"}%"></i></div></div><div class="home-grid">${tile("РЕЖИМЫ", "7", "сценариев", "nav:modes", "accent")}${tile("НАСТРОЙКИ", "", "оборудование и параметры", "nav:settings", "info")}${tile("ТЕМПЕРАТУРЫ", "6", "датчиков", "nav:temps", "warn")}${tile("СЕРВИС", "", "проверка и диагностика", "nav:service", "success")}</div>`;
  }

  function renderModes() {
    const modes = [["Ректификация", "очистка и отбор", "accent"], ["Дистилляция", "отбор по объёму", "info"], ["Ручная ректификация", "ручной сценарий", ""], ["Затирание", "температурные шаги", "success"], ["Выдержка", "температура и время", "warn"], ["НБК", "непрерывная колонна", "info"], ["Брожение", "двухзонный контроль", "accent"]];
    return `${title(true)}<div class="notice">Выбор режима не запускает процесс: дальше откроется подготовка.</div><div class="mode-grid">${modes.map(([name, desc, tone]) => button(name, `mode:${name}`, `mode-button ${tone}`, desc)).join("")}<div class="empty">7 поддерживаемых<br>режимов</div></div>`;
  }

  function renderSetup() {
    return `${title(true)}<div class="notice good">Режим выбран: <strong>${esc(state.selectedMode)}</strong>. Настройте профиль и проверьте готовность.</div><div class="section-grid"><div class="detail-list">${list("Активный профиль", state.profile, "nav:profiles")}${list("Основные параметры", "температура, мощность", "nav:params")}${list("Предпусковая проверка", state.preflightReady ? "готово" : "требуется", "nav:preflight")}</div><div class="notice"><strong>Маршрут запуска</strong><br><br>Профиль → параметры → предпуск → старт. В эмуляторе параметры меняются локально и не сохраняются в прошивку.</div></div>`;
  }

  function renderProfiles() {
    const profiles = ["Спирт сырец · 3500 Вт", "Нейтраль · 1800 Вт", "Ароматный · 2200 Вт", "Быстрый тест · 1000 Вт"];
    return `${title(true)}<div class="notice">Выберите совместимый профиль. Он станет черновиком до запуска.</div><div class="detail-list">${profiles.map((p, i) => list(p, p === state.profile ? "ВЫБРАН" : "", `profile:${i}`)).join("")}</div>`;
  }

  function renderPreflight() {
    const ready = state.preflightReady;
    return `${title(true)}<div class="notice ${ready ? "good" : "warn"}">${ready ? "Проверка пройдена. Запуск разрешён." : "Обновите проверку перед запуском."}</div><div class="check-list"><div class="check-item"><i class="check">✓</i>Датчики температуры на связи</div><div class="check-item"><i class="check">✓</i>ТЭН и насос доступны</div><div class="check-item"><i class="check ${ready ? "" : "warn"}">${ready ? "✓" : "!"}</i>${ready ? "Профиль и safety подтверждены" : "Нужна актуальная проверка"}</div></div><div class="edit-actions" style="margin-top:4%"><button data-action="preflight-refresh" class="cancel">ОБНОВИТЬ</button><button data-action="start-process" class="save" style="grid-column:span 3">СТАРТ</button></div>`;
  }

  function renderMonitor() {
    return `${title()}<div class="status-card"><div class="status-main"><span>${esc(state.selectedMode)}</span><strong>${state.paused ? "ПАУЗА" : "РАБОТА"}</strong></div><div class="screen-note">Фаза: отбор тела · осталось 00:27</div><div class="progress"><i></i></div></div><div class="card-grid">${tile("КУБ", "78.2", "°C", "nav:temps", "warn")}${tile("ЦАРГА", "77.8", "°C", "nav:temps", "accent")}${tile("МОЩНОСТЬ", "1800", "Вт", "nav:params", "info")}${tile("ОТБОР", "1.2", "л/ч", "nav:control", "success")}</div>`;
  }

  function renderControl() {
    const pause = state.paused ? "ПРОДОЛЖИТЬ" : "ПАУЗА";
    return `${title()}<div class="notice">Контекстное меню активного процесса.</div><div class="control-grid">${button("МОНИТОР", "nav:monitor", "control-button accent", "текущие значения")}${button("ПАРАМЕТРЫ", "nav:params", "control-button info", "доступные уставки")}${button("БЕЗОПАСНОСТЬ", "nav:safety", "control-button warn", "датчики и лимиты")}${button("СОБЫТИЯ", "nav:events", "control-button", "последние сообщения")}${button("РУЧНОЙ РЕЖИМ", "nav:manual", "control-button", "исполнительные узлы")}${button(pause, "toggle-pause", `control-button ${state.paused ? "success" : "warn"}`, "остановить / продолжить")}</div><button class="tile danger" style="width:100%;height:13%;margin-top:1.5%" data-action="stop-request"><span class="tile-label">ОСТАНОВИТЬ ПРОЦЕСС</span></button>`;
  }

  function renderSettings() {
    const theme = state.theme === "dark" ? "ТЁМНАЯ" : "СВЕТЛАЯ";
    return `${title()}<div class="card-grid">${tile("ОБОРУДОВАНИЕ", `${state.values.heater}`, "Вт", "nav:equipment", "accent")}${tile("РЕКТИФИКАЦИЯ", "Головы / тело", "", "nav:rect", "info")}${tile("ДИСТИЛЛЯЦИЯ", `${state.values.speed}`, "мл/ч", "nav:dist", "warn")}${tile("КАЛИБРОВКА", "Насос / touch", "", "nav:calibration", "success")}</div><div class="edit-actions" style="margin-top:2.4%"><button data-action="toggle-theme">ТЕМА: ${theme}</button><button data-action="toggle-sound">ЗВУК: ${state.sound ? "ВКЛ" : "ВЫКЛ"}</button><button data-action="toggle-lang">ЯЗЫК: ${state.lang.toUpperCase()}</button><button data-action="toggle-display">ЭКРАН: ${state.displayProfile}</button></div>`;
  }

  function renderEquipment() {
    return `${title(true)}<div class="detail-list">${data("Мощность ТЭНа", `${state.values.heater} Вт`, "edit:heater")}${data("Высота колонны", `${state.values.column} мм`, "edit:column")}${data("Мешалка", state.stirrer ? "ВКЛ" : "ВЫКЛ", "toggle-stirrer")}${data("Насос", "12.00 мл/об", "nav:pump")}</div><div class="notice" style="margin-top:3%">Нажмите значение, чтобы открыть редактор. Диапазоны — демонстрационные.</div>`;
  }

  function renderRect() {
    const first = state.rectPage === 0;
    const rows = first ? [["Сырьё", "Сахарная брага"], ["Головы", "5 %"], ["Тело", "90 %"], ["Хвосты", "5 %"]] : [["Флегмовое число", `${state.values.reflux}`], ["Температура верха", "78.2 °C"], ["Температура низа", "90.5 °C"], ["Отбор тела", "1.2 л/ч"]];
    return `${title(true)}<div class="notice">${first ? "Сырьё и отборы" : "Поток и температура"}</div><div class="detail-list">${rows.map(([a,b]) => data(a,b,a === "Флегмовое число" ? "edit:reflux" : "")).join("")}</div><button class="tile info" style="width:100%;height:16%;margin-top:3%" data-action="rect-page"><span class="tile-label">${first ? "ПОТОК И ТЕМПЕРАТУРА" : "СЫРЬЁ И ОТБОРЫ"}</span></button>`;
  }

  function renderDist() {
    return `${title(true)}<div class="detail-list">${data("Скорость отбора", `${state.values.speed} мл/ч`, "edit:speed")}${data("Целевой объём", `${state.values.volume} мл`, "edit:volume")}${data("Начало отбора", "78.5 °C")}${data("Остановить при", "92.0 °C")}</div><div class="notice" style="margin-top:3%">Скорость и объём открывают универсальный редактор значений.</div>`;
  }

  function renderCalibration() {
    return `${title(true)}<div class="detail-list">${list("Калибровка насоса", "прокачка → измерение", "nav:pump")}${list("Калибровка touch", "координаты сохранены", "toast:Touch-калибровка в эмуляторе не требуется")}${list("Датчики температуры", "6 датчиков", "nav:temps")}</div><div class="notice">Сервисные действия в эмуляторе не управляют исполнительными устройствами.</div>`;
  }

  function renderPump() {
    const steps = [["ПРОКАЧКА", "Прокачайте линию перед измерением"], ["ИЗМЕРЕНИЕ", "Соберите поданный объём в мерную тару"], ["ВВОД ОБЪЁМА", "Введите фактический объём и сохраните"], ["ГОТОВО", "Калибровка насоса сохранена в демо"]];
    const [name, text] = steps[state.pumpStep];
    return `${title(true)}<div class="notice good"><strong>Шаг ${state.pumpStep + 1} из 4 · ${name}</strong><br>${text}</div><div class="editor"><div class="unit">Калибровочный коэффициент</div><div class="edit-value">12.00 <small>мл/об</small></div><div class="edit-actions"><button class="cancel" data-action="pump-back">НАЗАД</button><button class="save" style="grid-column:span 3" data-action="pump-next">${state.pumpStep === 3 ? "ЗАВЕРШИТЬ" : "ДАЛЕЕ"}</button></div></div>`;
  }

  function renderTemps() {
    const rows = [["Куб", "78.2 °C"], ["Царга, низ", "77.8 °C"], ["Царга, верх", "77.4 °C"], ["Дефлегматор", "31.6 °C"], ["Вода вход", "18.4 °C"], ["Вода выход", "26.9 °C"]];
    return `${title(true)}<div class="detail-list">${rows.map(([a,b]) => data(a,b)).join("")}</div>`;
  }

  function renderSafety() {
    return `${title(true)}<div class="notice ${state.safety ? "good" : "danger"}"><strong>${state.safety ? "SAFE · система в норме" : "ALARM · требуется подтверждение"}</strong><br>${state.safety ? "Все обязательные датчики доступны, аварийных условий нет." : "Демо-тревога: имитация срабатывания контура безопасности."}</div><div class="detail-list">${data("Датчики", state.safety ? "6 / 6 доступны" : "проверить")}${data("Перегрев", "не обнаружен")}${data("Защита по воде", "в норме")}</div>${state.safety ? "" : `<button class="tile success" style="width:100%;height:17%;margin-top:3%" data-action="ack-safety"><span class="tile-label">ПОДТВЕРДИТЬ И СБРОСИТЬ</span></button>`}`;
  }

  function renderEvents() {
    const rows = [["12:06", "Фаза отбора запущена", "✓"], ["12:05", "Профиль применён", "✓"], ["12:04", "Preflight пройден", "✓"], ["12:01", "Оператор открыл управление", "i"]];
    return `${title(true)}<div class="detail-list">${rows.map(([time, msg, icon]) => `<div class="data-row"><span>${time} · ${msg}</span><strong>${icon}</strong></div>`).join("")}</div><button class="tile info" style="width:100%;height:16%;margin-top:3%" data-action="toast:Снимок событий обновлён"><span class="tile-label">ОБНОВИТЬ СНИМОК</span></button>`;
  }

  function renderParams() {
    return `${title(true)}<div class="detail-list">${data("Мощность", `${state.values.heater} Вт`, "edit:heater")}${data("Скорость отбора", `${state.values.speed} мл/ч`, "edit:speed")}${data("Температура цели", `${state.values.temperature.toFixed(1)} °C`, "edit:temperature")}${data("Флегмовое число", `${state.values.reflux.toFixed(1)}`, "edit:reflux")}</div><div class="notice" style="margin-top:3%">В реальном TFT здесь выводятся только разрешённые для текущего режима уставки.</div>`;
  }

  function renderManual() {
    const item = (key, label, tone) => button(`${label}: ${state.output[key] ? "ВКЛ" : "ВЫКЛ"}`, `output:${key}`, `control-button ${state.output[key] ? tone : ""}`, state.output[key] ? "демо-выход активен" : "демо-выход выключен");
    return `${title(true)}<div class="notice warn">Ручное управление доступно как демонстрация: реальные GPIO не вызываются.</div><div class="control-grid">${item("heater", "ТЭН", "warn")}${item("pump", "НАСОС", "info")}${item("stirrer", "МЕШАЛКА", "accent")}${item("valve", "КЛАПАН", "success")}${button("ВСЁ ВЫКЛ", "outputs-off", "control-button danger", "безопасное состояние")}${button("НАЗАД", "back", "control-button", "к управлению")}</div>`;
  }

  function renderService() {
    return `${title()}<div class="notice">Wi‑Fi: <strong>192.168.4.1</strong> · Версия: <strong>Smart Column S3 demo</strong></div><div class="card-grid">${tile("ТЕМПЕРАТУРЫ", "6", "датчиков", "nav:temps", "info")}${tile("ПРОВЕРКА", "", "оборудование", "nav:serviceTest", "warn")}${tile("ДОЗИРОВАНИЕ", "", "насос по объёму", "nav:dosing", "accent")}${tile("АПТАЙМ", "02:14", "", "toast:Диагностика дисплея: норма", "success")}</div>`;
  }

  function renderServiceTest() {
    return `${title(true)}<div class="notice good">READY · Тест доступен в IDLE или через подтверждённую сервисную паузу.</div><div class="control-grid">${button("ТЭН", "toast:Тест ТЭНа: виртуальный", "control-button warn", "выход выключен")}${button("НАСОС", "toast:Тест насоса: виртуальный", "control-button info", "выход выключен")}${button("МЕШАЛКА", "toast:Тест мешалки: виртуальный", "control-button accent", "выход выключен")}${button("КЛАПАНЫ", "toast:Тест клапанов: виртуальный", "control-button success", "выход выключен")}${button("ДАТЧИКИ", "nav:temps", "control-button", "показать значения")}${button("ЗАВЕРШИТЬ", "back", "control-button", "вернуться в сервис")}</div>`;
  }

  function renderDosing() {
    return `${title(true)}<div class="notice">Сервисный маршрут дозирования. Значения являются безопасным макетом.</div><div class="detail-list">${data("Целевой объём", "250 мл", "toast:Редактор дозирования появится в следующем срезе")}${data("Скорость", "600 мл/ч")}${data("Подача", "остановлена")}</div><button class="tile accent" style="width:100%;height:18%;margin-top:3%" data-action="toast:Дозирование запущено в режиме демо"><span class="tile-label">ПОДТВЕРДИТЬ ЗАПУСК</span></button>`;
  }

  const editorMeta = {
    heater: { label: "МОЩНОСТЬ ТЭНА", unit: "Вт", step: 100, min: 500, max: 5000, digits: 0 },
    column: { label: "ВЫСОТА КОЛОННЫ", unit: "мм", step: 10, min: 200, max: 3000, digits: 0 },
    speed: { label: "СКОРОСТЬ ОТБОРА", unit: "мл/ч", step: 50, min: 50, max: 5000, digits: 0 },
    volume: { label: "ЦЕЛЕВОЙ ОБЪЁМ", unit: "мл", step: 100, min: 100, max: 10000, digits: 0 },
    reflux: { label: "ФЛЕГМОВОЕ ЧИСЛО", unit: "", step: .1, min: 0.5, max: 20, digits: 1 },
    temperature: { label: "ТЕМПЕРАТУРА ЦЕЛИ", unit: "°C", step: .1, min: 10, max: 100, digits: 1 }
  };
  function renderEditor() {
    const m = editorMeta[state.editor.key];
    const value = Number(state.editor.value).toFixed(m.digits);
    return `<div class="editor"><div class="screen-title"><span>${m.label}</span><button class="back" data-action="editor-cancel">ОТМЕНА</button></div><div class="unit">Диапазон ${m.min}–${m.max} ${m.unit}</div><div class="edit-value">${value} <small>${m.unit}</small></div><div class="edit-actions"><button class="minus" data-action="editor:-1">−</button><button class="plus" data-action="editor:1">+</button><button class="save" data-action="editor:save">СОХРАНИТЬ</button><button class="cancel" data-action="editor-cancel">ОТМЕНА</button></div></div>`;
  }

  function overlay() {
    if (!state.overlay) return "";
    return `<div class="overlay"><div class="modal"><h3>${esc(state.overlay.title)}</h3><p>${esc(state.overlay.message)}</p><div class="modal-actions"><button data-action="overlay-cancel">ОТМЕНА</button><button class="confirm" data-action="overlay-confirm">ПОДТВЕРДИТЬ</button></div></div></div>`;
  }

  function content() {
    if (state.editor) return renderEditor();
    const renderers = { home: renderHome, modes: renderModes, setup: renderSetup, profiles: renderProfiles, preflight: renderPreflight, monitor: renderMonitor, control: renderControl, settings: renderSettings, equipment: renderEquipment, rect: renderRect, dist: renderDist, calibration: renderCalibration, pump: renderPump, temps: renderTemps, safety: renderSafety, events: renderEvents, params: renderParams, manual: renderManual, service: renderService, serviceTest: renderServiceTest, dosing: renderDosing };
    return (renderers[state.screen] || renderHome)();
  }

  function render() {
    const mode = state.running ? state.selectedMode : "IDLE";
    const process = state.running ? (state.paused ? "Пауза" : "Процесс идёт") : "Ожидание";
    scenarioStatus.textContent = `${mode} · ${process} · ${state.safety ? "SAFE" : "ALARM"}`;
    tft.className = `tft ${state.theme === "light" ? "light" : ""}`;
    tft.innerHTML = `${header()}<section class="screen-content">${content()}</section>${state.editor ? `<div class="screen-tabs"><button data-action="editor-cancel">ОТМЕНА</button><span class="footer-spacer"></span><span class="footer-spacer"></span><span class="footer-spacer"></span></div>` : footer()}${overlay()}`;
    applyZoom();
  }

  function resetDemo() { state = initialState(); render(); }
  function applyZoom() {
    const percent = Math.round(state.zoom * 100);
    const width = 480 * state.zoom;
    const height = 320 * state.zoom;
    tft.style.transform = `scale(${state.zoom})`;
    tftViewport.style.width = `${width}px`;
    tftViewport.style.height = `${height}px`;
    zoomRange.value = String(percent);
    zoomValue.value = `${percent}%`;
    zoomValue.textContent = `${percent}%`;
  }
  function setZoom(percent) {
    state.zoom = Math.max(.5, Math.min(2, percent / 100));
    applyZoom();
  }
  function openEditor(key) { state.editor = { key, value: state.values[key], origin: state.screen }; render(); }
  function toast(message) { state.toast = message; state.overlay = { title: "ДЕМО", message }; render(); }

  document.addEventListener("click", event => {
    const target = event.target.closest("[data-action]");
    if (!target) return;
    const action = target.dataset.action;
    if (action === "home") return goHome();
    if (action === "back") return goBack();
    if (action === "reset") return resetDemo();
    if (action === "zoom-out") return setZoom(Math.round(state.zoom * 100) - 25);
    if (action === "zoom-in") return setZoom(Math.round(state.zoom * 100) + 25);
    if (action === "zoom-actual") return setZoom(100);
    if (action === "scenario-idle") { state.running = false; state.paused = false; state.safety = true; goHome(); return; }
    if (action === "scenario-run") { state.running = true; state.paused = false; state.safety = true; state.screen = "monitor"; state.history = []; render(); return; }
    if (action === "scenario-alarm") { state.safety = false; state.screen = "safety"; render(); return; }
    if (action.startsWith("nav:")) return navigate(action.slice(4));
    if (action.startsWith("mode:")) { state.selectedMode = action.slice(5); state.profile = "Профиль не выбран"; state.preflightReady = false; return navigate("setup"); }
    if (action.startsWith("profile:")) { const profiles = ["Спирт сырец · 3500 Вт", "Нейтраль · 1800 Вт", "Ароматный · 2200 Вт", "Быстрый тест · 1000 Вт"]; state.profile = profiles[Number(action.slice(8))]; return goBack(); }
    if (action === "preflight-refresh") { state.preflightReady = true; return render(); }
    if (action === "start-process") { if (!state.preflightReady) return toast("Сначала нажмите «Обновить» и дождитесь готовности preflight."); state.running = true; state.paused = false; state.screen = "monitor"; state.history = []; return render(); }
    if (action === "toggle-pause") { state.paused = !state.paused; return render(); }
    if (action === "stop-request") { state.overlay = { title: "ОСТАНОВИТЬ ПРОЦЕСС?", message: "Эмулятор переведёт сценарий в IDLE. Реальные выходы не затрагиваются." }; return render(); }
    if (action === "overlay-cancel") { state.overlay = null; return render(); }
    if (action === "overlay-confirm") { state.overlay = null; state.running = false; state.paused = false; state.screen = "home"; state.history = []; return render(); }
    if (action === "toggle-theme") { state.theme = state.theme === "dark" ? "light" : "dark"; return render(); }
    if (action === "toggle-sound") { state.sound = !state.sound; return render(); }
    if (action === "toggle-lang") { state.lang = state.lang === "ru" ? "en" : "ru"; return render(); }
    if (action === "toggle-display") { state.displayProfile = state.displayProfile === "BAL" ? "FAST" : state.displayProfile === "FAST" ? "SAFE" : "BAL"; return render(); }
    if (action === "toggle-stirrer") { state.stirrer = !state.stirrer; return render(); }
    if (action === "rect-page") { state.rectPage = state.rectPage ? 0 : 1; return render(); }
    if (action === "pump-next") { state.pumpStep = state.pumpStep === 3 ? 0 : state.pumpStep + 1; return render(); }
    if (action === "pump-back") { state.pumpStep = Math.max(0, state.pumpStep - 1); return render(); }
    if (action === "ack-safety") { state.safety = true; return render(); }
    if (action.startsWith("output:")) { const key = action.slice(7); state.output[key] = !state.output[key]; return render(); }
    if (action === "outputs-off") { Object.keys(state.output).forEach(k => { state.output[k] = false; }); return render(); }
    if (action.startsWith("edit:")) return openEditor(action.slice(5));
    if (action.startsWith("editor:")) {
      const what = action.slice(7);
      if (what === "save") { state.values[state.editor.key] = state.editor.value; state.editor = null; return render(); }
      const m = editorMeta[state.editor.key];
      state.editor.value = Math.min(m.max, Math.max(m.min, +(state.editor.value + Number(what) * m.step).toFixed(m.digits)));
      return render();
    }
    if (action === "editor-cancel") { state.editor = null; return render(); }
    if (action.startsWith("toast:")) return toast(action.slice(6));
  });

  zoomRange.addEventListener("input", event => setZoom(Number(event.target.value)));

  window.addEventListener("keydown", event => {
    if (event.key === "Escape") { if (state.overlay) { state.overlay = null; render(); } else if (state.editor) { state.editor = null; render(); } else goBack(); }
    if (event.key === "Home") goHome();
    if (event.key === "+") setZoom(Math.round(state.zoom * 100) + 25);
    if (event.key === "-") setZoom(Math.round(state.zoom * 100) - 25);
  });

  render();
}());
