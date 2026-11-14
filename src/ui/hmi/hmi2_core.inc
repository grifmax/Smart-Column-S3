// HMI v2 — каркас главного экрана, геометрия «B» (согласована по макету):
// шапка 32 px + 4 строки x 60 px + нижняя панель 48 px = 320 px ровно.
// Остальные экраны пока остаются старыми.
// ============================================================================
static constexpr int16_t HMI2_HEADER_H = 32;
static constexpr int16_t HMI2_ROW_H = 60;
static constexpr int16_t HMI2_ROW_GAP = 0;
static constexpr int16_t HMI2_FOOTER_H = 48;
static constexpr uint8_t HMI2_VISIBLE_ROWS = HmiPage::kVisibleRows;
// Keep these coordinates identical to docs/prototype/tft-menu-v3.html.
// The prototype is rendered at native 480x320, so every value here is a TFT
// pixel rather than a scaled design unit.
static constexpr int16_t HMI2_MARGIN_X = 0;
static constexpr int16_t HMI2_RAIL_W = 48;
static constexpr int16_t HMI2_NAV_Y = 1;
static constexpr int16_t HMI2_NAV_H = 30;
static constexpr int16_t HMI2_NAV_W = 116;
static constexpr int16_t HMI2_NAV_GAP = 2;
static constexpr int16_t HMI2_NAV_LEFT_X = TFT_WIDTH - (HMI2_NAV_W * 2 + HMI2_NAV_GAP);
static constexpr int16_t HMI2_NAV_RIGHT_X = HMI2_NAV_LEFT_X + HMI2_NAV_W + HMI2_NAV_GAP;

static uint8_t hmi2Section = 0;
static uint8_t hmi2Scroll = 0;
static bool hmi2TouchCalibrationPage = false;
static char hmi2TouchCalibrationMessage[64] = {0};
// Full-screen list of every DS18B20 channel; auto-opened when a process,
// pump calibration or dosing pour starts (see updateTempsAutoFocus).
static bool hmi2TempsPage = false;
static uint8_t hmi2TempsReturnSection = 6;
static uint8_t hmi2TempsReturnScroll = 0;
enum class Hmi2Subpage : uint8_t {
  None, Modes, Profiles, Journal, Wifi, Sensors, SensorAddresses, SensorOffsets,
  WifiNetworks, WifiPassword
};
static Hmi2Subpage hmi2Subpage = Hmi2Subpage::None;
static uint8_t hmi2SensorRole = 0;
static uint8_t g_hmi2ScanCount = 0;
static uint8_t g_hmi2ScanAddresses[10][8] = {};
static char g_hmi2ScanLabel[10][16] = {{0}};
static char hmi2SensorMessage[64] = {0};
static char hmi2WifiSsid[64] = {0};
static HmiKeyboard::State hmi2WifiKeyboard;
static char hmi2WifiMessage[64] = {0};
static uint8_t hmi2WifiNetworkCount = 0;
static char hmi2WifiNetworks[10][33] = {{0}};
static UiScreen hmi2LastScreen = static_cast<UiScreen>(255);
static Mode const kHmi2Modes[] = {Mode::RECTIFICATION, Mode::DISTILLATION,
    Mode::MANUAL_RECT, Mode::MASHING, Mode::HOLD, Mode::NBK,
    Mode::FERMENTATION, Mode::AUTOCLAVE};
static TftTempsFocusLogic::State g_hmi2TempsFocus;

static const char *const kHmi2Sections[] = {
    "ГЛАВНАЯ", "РЕЖИМ", "НАСТРОЙКИ 1/3", "НАСТРОЙКИ 2/3",
    "НАСТРОЙКИ 3/3", "БЕЗОПАСНОСТЬ", "СЕРВИС", "ПРОЦЕСС"};
static constexpr uint8_t kHmi2SectionCount =
    sizeof(kHmi2Sections) / sizeof(kHmi2Sections[0]);

enum class Hmi2RowType : uint8_t { Link, Read, Value, Bool, Notice };
enum class Hmi2Field : uint8_t {
  None,
  HeaterPower,
  ColumnHeight,
  CubeVolume,
  PackingCoeff,
  MinHeaterSubmerge,
  BoosterEnabled,
  BoosterPower,
  BodyLevelSensorEnabled,
  LeakSensorEnabled,
  Ds2482Enabled,
  CoolingPwmHoldPercent,
  CoolingPwmPullMs,
  SafetyPressureMax,
  SafetyTsaMax,
  SafetyWaterOutMax,
  SafetyWaterOutRiseRate,
  SafetyPressureRiseRate,
  BrightnessPercent,
  DemoMode,
  MqttEnabled,
};

struct Hmi2Row {
  Hmi2RowType type;
  Hmi2Field field;
  const char *label;
  char value[64];
  char unit[20];
  const char *linkText;
  uint16_t tone;
};

static Hmi2Row g_hmi2Rows[16];
static uint8_t g_hmi2RowCount = 0;

static void hmi2Add(Hmi2RowType type, const char *label, const char *value,
                    const char *unit, const char *linkText, uint16_t tone,
                    Hmi2Field field = Hmi2Field::None) {
  if (g_hmi2RowCount >= (sizeof(g_hmi2Rows) / sizeof(g_hmi2Rows[0]))) {
    return;
  }
  Hmi2Row &row = g_hmi2Rows[g_hmi2RowCount++];
  row.type = type;
  row.field = field;
  row.label = label;
  row.linkText = linkText;
  row.tone = tone;
  row.value[0] = '\0';
  row.unit[0] = '\0';
  if (value != nullptr) {
    strncpy(row.value, value, sizeof(row.value) - 1);
    row.value[sizeof(row.value) - 1] = '\0';
  }
  if (unit != nullptr) {
    strncpy(row.unit, unit, sizeof(row.unit) - 1);
    row.unit[sizeof(row.unit) - 1] = '\0';
  }
}

static void hmi2FormatClock(char *out, size_t size) {
  time_t now = time(nullptr);
  struct tm info;
  if (now > 1600000000 && localtime_r(&now, &info) != nullptr) {
    snprintf(out, size, "%02d:%02d", info.tm_hour, info.tm_min);
  } else {
    snprintf(out, size, "--:--");
  }
}

static void hmi2FormatSensorAddress(const uint8_t address[8], char *out,
                                    size_t size) {
  snprintf(out, size, "%02X-%02X-%02X", address[5], address[6], address[7]);
}

static int8_t hmi2SensorRoleOwner(const uint8_t address[8],
                                 uint8_t exceptRole) {
  return SensorCalibrationLogic::roleOwner(g_settings.tempCal.addresses,
                                            address, exceptRole);
}

static uint8_t hmi2ScanSensors() {
  memset(g_hmi2ScanAddresses, 0, sizeof(g_hmi2ScanAddresses));
  g_hmi2ScanCount = Sensors::scanDS18B20(g_hmi2ScanAddresses);
  if (g_hmi2ScanCount > 10) g_hmi2ScanCount = 10;
  for (uint8_t i = 0; i < g_hmi2ScanCount; ++i) {
    hmi2FormatSensorAddress(g_hmi2ScanAddresses[i], g_hmi2ScanLabel[i],
                            sizeof(g_hmi2ScanLabel[i]));
  }
  return g_hmi2ScanCount;
}

static bool hmi2AssignSensorAddress(uint8_t role, const uint8_t address[8]) {
  if (role >= TEMP_COUNT) return false;
  const TempCalibration previous = g_settings.tempCal;
  if (!SensorCalibrationLogic::assignAddress(g_settings.tempCal.addresses, role,
                                             address)) {
    snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage),
             "%s", "Адрес не выбран");
    return false;
  }
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage),
             "%s", "Ошибка записи NVS");
    return false;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage),
           "%s", "Адрес назначен и сохранён");
  return true;
}

static bool hmi2ClearSensorRole(uint8_t role) {
  if (role >= TEMP_COUNT) return false;
  const TempCalibration previous = g_settings.tempCal;
  if (!SensorCalibrationLogic::clearAddress(g_settings.tempCal.addresses,
                                            role)) {
    return false;
  }
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    return false;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage),
           "%s", "Датчик отвязан, включено автоназначение");
  return true;
}

static uint8_t hmi2AutoAssignSensors() {
  const TempCalibration previous = g_settings.tempCal;
  const uint8_t assigned = SensorCalibrationLogic::autoAssign(
      g_settings.tempCal.addresses, g_hmi2ScanAddresses, g_hmi2ScanCount);
  if (!assigned) {
    snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage), "%s",
             "Свободных датчиков для назначения нет");
    return 0;
  }
  if (!NVSManager::saveSettings(g_settings)) {
    g_settings.tempCal = previous;
    snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage), "%s",
             "Ошибка записи NVS");
    return 0;
  }
  Sensors::applyCalibration(g_settings.tempCal);
  Sensors::refreshTemperatureInventory();
  snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage),
           "Назначено датчиков: %u", static_cast<unsigned>(assigned));
  return assigned;
}

static void hmi2ScanWifiNetworks() {
  hmi2WifiNetworkCount = 0;
  const int found = WiFi.scanNetworks();
  for (int i = 0; i < found && hmi2WifiNetworkCount < 10; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    snprintf(hmi2WifiNetworks[hmi2WifiNetworkCount],
             sizeof(hmi2WifiNetworks[0]), "%s", ssid.c_str());
    ++hmi2WifiNetworkCount;
  }
  snprintf(hmi2WifiMessage, sizeof(hmi2WifiMessage), "Найдено сетей: %u",
           static_cast<unsigned>(hmi2WifiNetworkCount));
}

static const char *hmi2CurrentSsid() {
  static char ssid[64];
  snprintf(ssid, sizeof(ssid), "%s",
           WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str()
                                         : g_settings.wifi.ssid);
  return ssid;
}

static void hmi2OpenWifiPassword(const char *ssid) {
  snprintf(hmi2WifiSsid, sizeof(hmi2WifiSsid), "%s", ssid != nullptr ? ssid : "");
  WiFiProfile existing;
  const bool saved = WiFiProfiles::getProfileBySsid(g_settings.wifi,
                                                   hmi2WifiSsid, existing);
  HmiKeyboard::init(hmi2WifiKeyboard, saved ? existing.password : "");
  hmi2WifiMessage[0] = '\0';
  hmi2Subpage = Hmi2Subpage::WifiPassword;
  hmi2Scroll = 0;
  requestFullScreenRedraw();
}

static void hmi2SaveWifiPassword() {
  WiFiProfile profile;
  if (!WiFiProfiles::getProfileBySsid(g_settings.wifi, hmi2WifiSsid, profile)) {
    memset(&profile, 0, sizeof(profile));
    profile.enabled = true;
    snprintf(profile.ssid, sizeof(profile.ssid), "%s", hmi2WifiSsid);
    snprintf(profile.subnet, sizeof(profile.subnet), "%s", "255.255.255.0");
  }
  snprintf(profile.password, sizeof(profile.password), "%s", hmi2WifiKeyboard.saved);
  const WiFiSettings previous = g_settings.wifi;
  if (!WiFiProfiles::upsertProfile(g_settings.wifi, profile, true) ||
      !NVSManager::saveSettings(g_settings)) {
    g_settings.wifi = previous;
    snprintf(hmi2WifiMessage, sizeof(hmi2WifiMessage), "%s",
             "Не удалось сохранить сеть");
    requestFullScreenRedraw();
    return;
  }
  snprintf(hmi2WifiMessage, sizeof(hmi2WifiMessage), "%s",
           "Пароль сохранён, идёт подключение");
  WiFiProfiles::beginConnection(profile);
  hmi2Subpage = Hmi2Subpage::Wifi;
  hmi2Scroll = 0;
  requestFullScreenRedraw();
}

static uint8_t hmi2BuildRows() {
  g_hmi2RowCount = 0;
  char buf[64];
  if (hmi2Subpage == Hmi2Subpage::Modes) {
    for (uint8_t i = 0; i < sizeof(kHmi2Modes) / sizeof(kHmi2Modes[0]); ++i)
      hmi2Add(Hmi2RowType::Link, getDisplayModeName(kHmi2Modes[i]), "",
              nullptr, "ВЫБРАТЬ", COLOR_INFO);
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::Profiles) {
    for (uint8_t i = 0; i < g_tftProfileOptionCount && i < 16; ++i)
      hmi2Add(Hmi2RowType::Link, g_tftProfileOptions[i].name, "", nullptr,
              "ВЫБРАТЬ", COLOR_INFO);
    if (!g_tftProfileOptionCount)
      hmi2Add(Hmi2RowType::Notice, "Совместимых профилей нет", "Выберите режим", nullptr, nullptr, COLOR_WARNING);
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::Journal) {
    static char labels[TFT_EVENT_CACHE_SIZE][24];
    for (uint8_t i = 0; i < g_tftEvents.count; ++i) {
      snprintf(labels[i], sizeof(labels[i]), "T+%lu с", static_cast<unsigned long>(g_tftEvents.timestamp[i] / 1000));
      hmi2Add(Hmi2RowType::Notice, labels[i], g_tftEvents.message[i], nullptr, nullptr,
              g_tftEvents.level[i] > 1 ? COLOR_DANGER : (g_tftEvents.level[i] ? COLOR_WARNING : COLOR_INFO));
    }
    if (!g_tftEvents.count)
      hmi2Add(Hmi2RowType::Notice, "Журнал пуст", "Событий пока нет", nullptr, nullptr, COLOR_INFO);
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::Wifi) {
    const bool connected = WiFi.status() == WL_CONNECTED;
    hmi2Add(Hmi2RowType::Read, "Wi-Fi", connected ? "ПОДКЛЮЧЕНО" : "НЕТ СВЯЗИ",
            nullptr, nullptr, connected ? COLOR_SUCCESS : COLOR_WARNING);
    hmi2Add(Hmi2RowType::Link, "Сеть", hmi2CurrentSsid(), nullptr, "СМЕНИТЬ",
            COLOR_INFO);
    hmi2Add(Hmi2RowType::Read, "IP",
            connected ? WiFi.localIP().toString().c_str() : "—", nullptr,
            nullptr, COLOR_INFO);
    char bufRssi[24];
    snprintf(bufRssi, sizeof(bufRssi), "%d dBm",
             connected ? static_cast<int>(WiFi.RSSI()) : 0);
    hmi2Add(Hmi2RowType::Read, "Сигнал", connected ? bufRssi : "—", nullptr,
            nullptr, COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Пароль и подключение", "", nullptr, "ВВОД",
            COLOR_PRIMARY);
    if (hmi2WifiMessage[0] != '\0') {
      hmi2Add(Hmi2RowType::Notice, "Результат", hmi2WifiMessage, nullptr,
              nullptr, COLOR_INFO);
    }
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::WifiNetworks) {
    hmi2Add(Hmi2RowType::Link, "Искать сети", "", nullptr, "СКАН", COLOR_PRIMARY);
    for (uint8_t i = 0; i < hmi2WifiNetworkCount; ++i) {
      const bool current =
          WiFi.status() == WL_CONNECTED && WiFi.SSID() == hmi2WifiNetworks[i];
      hmi2Add(Hmi2RowType::Link, hmi2WifiNetworks[i],
              current ? "текущая" : "", nullptr,
              current ? "ТЕКУЩАЯ" : "ВЫБРАТЬ",
              current ? COLOR_SUCCESS : COLOR_INFO);
    }
    if (!hmi2WifiNetworkCount) {
      hmi2Add(Hmi2RowType::Notice, "Сети не найдены", "Нажмите СКАН", nullptr,
              nullptr, COLOR_WARNING);
    }
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::Sensors) {
    for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
      const bool assigned = !SensorCalibrationLogic::isZeroAddress(
          g_settings.tempCal.addresses[role]);
      if (g_state.temps.valid[role]) {
        snprintf(buf, sizeof(buf), "%.1f C",
                 static_cast<double>(getSensorTemperatureValue(role, g_state)));
        for (char *c = buf; *c; ++c) if (*c == '.') *c = ',';
      } else {
        snprintf(buf, sizeof(buf), "%s", assigned ? "нет связи" : "—");
      }
      hmi2Add(Hmi2RowType::Link, getTempSensorLabel(role), buf, nullptr,
              assigned ? "СМЕНИТЬ" : "НАЗНАЧИТЬ",
              g_state.temps.valid[role] ? COLOR_INFO : COLOR_WARNING);
    }
    hmi2Add(Hmi2RowType::Link, "Автоназначение по порядку", "", nullptr,
            "ВЫПОЛНИТЬ", COLOR_PRIMARY);
    if (hmi2SensorMessage[0] != '\0') {
      hmi2Add(Hmi2RowType::Notice, "Результат", hmi2SensorMessage, nullptr,
              nullptr, COLOR_INFO);
    }
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::SensorAddresses) {
    hmi2Add(Hmi2RowType::Link, "Отвязать текущий датчик",
            getTempSensorLabel(hmi2SensorRole), nullptr, "ОТВЯЗАТЬ",
            COLOR_WARNING);
    for (uint8_t i = 0; i < g_hmi2ScanCount; ++i) {
      const int8_t owner =
          hmi2SensorRoleOwner(g_hmi2ScanAddresses[i], hmi2SensorRole);
      hmi2Add(Hmi2RowType::Link, g_hmi2ScanLabel[i],
              owner >= 0 ? getTempSensorLabel(static_cast<uint8_t>(owner))
                         : "свободен",
              nullptr, owner >= 0 ? "ЗАНЯТ" : "ВЫБРАТЬ",
              owner >= 0 ? COLOR_WARNING : COLOR_SUCCESS);
    }
    if (!g_hmi2ScanCount) {
      hmi2Add(Hmi2RowType::Notice, "Датчики не найдены",
              "Проверьте шину DS18B20 и повторите", nullptr, nullptr,
              COLOR_DANGER);
    }
    if (hmi2SensorMessage[0] != '\0') {
      hmi2Add(Hmi2RowType::Notice, "Результат", hmi2SensorMessage, nullptr,
              nullptr, COLOR_INFO);
    }
    return g_hmi2RowCount;
  }
  if (hmi2Subpage == Hmi2Subpage::SensorOffsets) {
    for (uint8_t role = 0; role < TEMP_COUNT; ++role) {
      snprintf(buf, sizeof(buf), "%+.1f", static_cast<double>(
          g_settings.tempCal.offsets[role]));
      for (char *c = buf; *c; ++c) if (*c == '.') *c = ',';
      hmi2Add(Hmi2RowType::Link, getTempSensorLabel(role), buf, "C",
              "ИЗМЕНИТЬ", COLOR_INFO);
    }
    return g_hmi2RowCount;
  }
  if (hmi2TouchCalibrationPage) {
    hmi2Add(Hmi2RowType::Link, "Начать калибровку", "", nullptr,
            touch_ok ? "НАЧАТЬ" : "НЕДОСТУПЕН",
            touch_ok ? COLOR_INFO : colorMuted());
    const char *status = hmi2TouchCalibrationMessage[0] != '\0'
                             ? hmi2TouchCalibrationMessage
                             : (g_settings.touchCal.valid
                                    ? "Координаты сохранены"
                                    : "Заводские координаты");
    hmi2Add(Hmi2RowType::Notice, "Статус", status, nullptr, nullptr,
            g_settings.touchCal.valid ? COLOR_SUCCESS : COLOR_WARNING);
    int16_t xMin = 0;
    int16_t xMax = 0;
    int16_t yMin = 0;
    int16_t yMax = 0;
    getTouchCalibration(&xMin, &xMax, &yMin, &yMax);
    snprintf(buf, sizeof(buf), "%d - %d", static_cast<int>(xMin),
             static_cast<int>(xMax));
    hmi2Add(Hmi2RowType::Read, "Ось X", buf, nullptr, nullptr, COLOR_INFO);
    snprintf(buf, sizeof(buf), "%d - %d", static_cast<int>(yMin),
             static_cast<int>(yMax));
    hmi2Add(Hmi2RowType::Read, "Ось Y", buf, nullptr, nullptr, COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Сброс калибровки", "", nullptr, "СБРОС",
            COLOR_WARNING);
    return g_hmi2RowCount;
  }
  if (hmi2Section == 0) {
    hmi2Add(Hmi2RowType::Link, "Режим", getDisplayModeName(ui.setupDraft.valid && g_state.mode == Mode::IDLE ? ui.setupDraft.mode : g_state.mode),
            nullptr, "ВЫБРАТЬ", COLOR_PRIMARY);
    if (g_state.temps.valid[TEMP_CUBE]) snprintf(buf, sizeof(buf), "%.1f", g_state.temps.cube);
    else snprintf(buf, sizeof(buf), "—");
    hmi2Add(Hmi2RowType::Read, "Куб", buf, "°C", nullptr, COLOR_INFO);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_state.power.power));
    hmi2Add(Hmi2RowType::Read, "Мощность", buf, "Вт", nullptr, COLOR_INFO);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_state.pump.running ? g_state.pump.speedMlPerHour : 0));
    hmi2Add(Hmi2RowType::Read, "Отбор", buf, "мл/ч", nullptr, COLOR_INFO);
    const bool alarm = Safety::isLatched(g_state);
    hmi2Add(Hmi2RowType::Link, "Безопасность", alarm ? "АВАРИЯ" : "НОРМА",
            nullptr, "ОТКРЫТЬ", alarm ? COLOR_DANGER : COLOR_SUCCESS);
    hmi2Add(Hmi2RowType::Link, "Сервис", "", nullptr, "ОТКРЫТЬ",
            COLOR_PRIMARY);
  } else if (hmi2Section == 1) { // РЕЖИМ
    hmi2Add(Hmi2RowType::Link, "Режим", getDisplayModeName(ui.setupDraft.valid && g_state.mode == Mode::IDLE ? ui.setupDraft.mode : g_state.mode),
            nullptr, "ВЫБРАТЬ", COLOR_PRIMARY);
    static char profileBuf[28];
    snprintf(profileBuf, sizeof(profileBuf), "%s",
             ui.setupDraft.valid ? ui.setupDraft.profileName : getActiveProfileName().c_str());
    hmi2Add(Hmi2RowType::Link, "Профиль", profileBuf, nullptr, "ВЫБРАТЬ",
            COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Параметры режима", "", nullptr,
            "НАСТРОИТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Notice, "Перед стартом",
            "проверка 5 пунктов", nullptr, nullptr, COLOR_WARNING);
    hmi2Add(Hmi2RowType::Link, "Старт процесса", "", nullptr, "СТАРТ",
            COLOR_SUCCESS);
  } else if (hmi2Section == 2) { // НАСТРОЙКИ 1/3 — оборудование
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_settings.equipment.heaterPowerW));
    hmi2Add(Hmi2RowType::Value, "Мощн. ТЭНа", buf, "Вт", nullptr,
            COLOR_WARNING, Hmi2Field::HeaterPower);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_settings.equipment.columnHeightMm));
    hmi2Add(Hmi2RowType::Value, "Высота колл.", buf, "мм", nullptr,
            COLOR_INFO, Hmi2Field::ColumnHeight);
    snprintf(buf, sizeof(buf), "%.1f", g_settings.equipment.cubeVolumeL);
    hmi2Add(Hmi2RowType::Value, "Объём куба", buf, "л", nullptr,
            COLOR_SUCCESS, Hmi2Field::CubeVolume);
    snprintf(buf, sizeof(buf), "%.1f", g_settings.equipment.packingCoeff);
    hmi2Add(Hmi2RowType::Value, "Коэф. РПН", buf, nullptr, nullptr,
            COLOR_PRIMARY, Hmi2Field::PackingCoeff);
    snprintf(buf, sizeof(buf), "%.1f",
             g_settings.equipment.minHeaterSubmergeL);
    hmi2Add(Hmi2RowType::Value, "Мин. погр. ТЭНа", buf, "л", nullptr,
            COLOR_WARNING, Hmi2Field::MinHeaterSubmerge);
    hmi2Add(Hmi2RowType::Bool, "Booster ТЭН",
            g_settings.equipment.boosterHeaterEnabled ? "1" : "0", nullptr,
            nullptr, COLOR_SUCCESS, Hmi2Field::BoosterEnabled);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_settings.equipment.boosterHeaterPowerW));
    hmi2Add(Hmi2RowType::Value, "Booster", buf, "Вт", nullptr,
            COLOR_INFO, Hmi2Field::BoosterPower);
  } else if (hmi2Section == 3) { // НАСТРОЙКИ 2/3 — датчики и насос
    hmi2Add(Hmi2RowType::Link, "Назначение датчиков темп.", "", nullptr,
            "ОТКРЫТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Калибр. датчиков темп.", "", nullptr,
            "ОТКРЫТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Калибр. насоса", "", nullptr, "ОТКРЫТЬ",
            COLOR_INFO);
    hmi2Add(Hmi2RowType::Bool, "Датчик уровня тела",
            g_settings.equipment.bodyLevelSensorEnabled ? "1" : "0", nullptr,
            nullptr, COLOR_SUCCESS, Hmi2Field::BodyLevelSensorEnabled);
    hmi2Add(Hmi2RowType::Bool, "Датчик протечки",
            g_settings.equipment.leakSensorEnabled ? "1" : "0", nullptr,
            nullptr, COLOR_SUCCESS, Hmi2Field::LeakSensorEnabled);
    hmi2Add(Hmi2RowType::Bool, "DS2482 для температур",
            g_settings.equipment.useDs2482ForTemps ? "1" : "0", nullptr,
            nullptr, COLOR_SUCCESS, Hmi2Field::Ds2482Enabled);
    const bool coolingValveOpen = Valves::getStartStop() > 0;
    hmi2Add(Hmi2RowType::Read, "Клапан охлаждения",
            coolingValveOpen ? "ОТКРЫТ" : "ЗАКРЫТ", nullptr, nullptr,
            coolingValveOpen ? COLOR_SUCCESS : colorMuted());
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_settings.equipment.coolingPwmPullMs));
    hmi2Add(Hmi2RowType::Value, "Втягивание", buf, "мс", nullptr,
            COLOR_WARNING, Hmi2Field::CoolingPwmPullMs);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(
                 g_settings.equipment.coolingPwmHoldPercent));
    hmi2Add(Hmi2RowType::Value, "Удержание", buf, "%", nullptr,
            COLOR_INFO, Hmi2Field::CoolingPwmHoldPercent);
    const uint8_t holdDuty = CoolingPwmLogic::percentToDuty(
        g_settings.equipment.coolingPwmHoldPercent);
    snprintf(buf, sizeof(buf), "%u мс · %u%% (%u/255) · %u кГц",
             static_cast<unsigned>(g_settings.equipment.coolingPwmPullMs),
             static_cast<unsigned>(
                 g_settings.equipment.coolingPwmHoldPercent),
             static_cast<unsigned>(holdDuty),
             static_cast<unsigned>(PWM_FREQ_VALVE / 1000U));
    hmi2Add(Hmi2RowType::Notice, "Ток удержания", buf, nullptr, nullptr,
            COLOR_INFO);
  } else if (hmi2Section == 4) { // СИСТЕМА
    static char network[64];
    snprintf(network, sizeof(network), "%s", WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "Нет связи");
    hmi2Add(Hmi2RowType::Link, "Wi-Fi", network, nullptr, "ОТКРЫТЬ", COLOR_INFO);
    char clock[8]; hmi2FormatClock(clock, sizeof(clock));
    hmi2Add(Hmi2RowType::Read, "Время", clock, nullptr, nullptr, COLOR_INFO);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_settings.displaySettings.brightness * 100U / 255U));
    hmi2Add(Hmi2RowType::Value, "Яркость", buf, "%", nullptr, COLOR_INFO,
            Hmi2Field::BrightnessPercent);
    hmi2Add(Hmi2RowType::Bool, "Демо-режим",
            g_settings.demoMode ? "1" : "0", nullptr, nullptr, COLOR_SUCCESS,
            Hmi2Field::DemoMode);
    hmi2Add(Hmi2RowType::Bool, "MQTT", g_settings.mqtt.enabled ? "1" : "0",
            nullptr, nullptr, COLOR_SUCCESS, Hmi2Field::MqttEnabled);
    hmi2Add(Hmi2RowType::Notice, "Системные изменения", "Через веб-настройки", nullptr, nullptr, COLOR_WARNING);
    hmi2Add(Hmi2RowType::Read, "Версия ПО", FW_VERSION, nullptr, nullptr, COLOR_INFO);
  } else if (hmi2Section == 5) { // БЕЗОПАСНОСТЬ — пороговые значения
    snprintf(buf, sizeof(buf), "%.0f", g_settings.safety.pressureMaxMmHg);
    hmi2Add(Hmi2RowType::Value, "Давл. макс.", buf, "мм рт.ст.",
            nullptr, COLOR_DANGER, Hmi2Field::SafetyPressureMax);
    snprintf(buf, sizeof(buf), "%.0f", g_settings.safety.tsaMaxC);
    hmi2Add(Hmi2RowType::Value, "TSA макс.", buf, "°C", nullptr,
            COLOR_WARNING, Hmi2Field::SafetyTsaMax);
    snprintf(buf, sizeof(buf), "%.0f", g_settings.safety.waterOutMaxC);
    hmi2Add(Hmi2RowType::Value, "Вода выход макс.", buf, "°C", nullptr,
            COLOR_INFO, Hmi2Field::SafetyWaterOutMax);
    snprintf(buf, sizeof(buf), "%.1f",
             g_settings.safety.waterOutRiseRateCMin);
    hmi2Add(Hmi2RowType::Value, "Вода рост", buf, "°C/мин", nullptr,
            COLOR_WARNING, Hmi2Field::SafetyWaterOutRiseRate);
    snprintf(buf, sizeof(buf), "%.0f",
             g_settings.safety.pressureRiseRateMmHgMin);
    hmi2Add(Hmi2RowType::Value, "Давл. рост", buf, "мм/мин", nullptr,
            COLOR_DANGER, Hmi2Field::SafetyPressureRiseRate);
    hmi2Add(Hmi2RowType::Link, "Проверка оборуд.", "", nullptr, "ОТКРЫТЬ",
            COLOR_WARNING);
    hmi2Add(Hmi2RowType::Notice, "При аварии", "Квитируйте, затем сбросьте",
            nullptr, nullptr, COLOR_WARNING);
  } else if (hmi2Section == 7) { // ПРОЦЕСС
    snprintf(buf, sizeof(buf), "%s / %s%s", getDisplayModeName(g_state.mode),
             getDisplayPhaseName(g_state), g_state.paused ? " · ПАУЗА" : "");
    hmi2Add(Hmi2RowType::Read, "Режим", buf, nullptr, nullptr, COLOR_INFO);
    if (g_state.temps.valid[TEMP_CUBE]) snprintf(buf, sizeof(buf), "%.1f", g_state.temps.cube);
    else snprintf(buf, sizeof(buf), "—");
    hmi2Add(Hmi2RowType::Read, "Куб", buf, "°C", nullptr, COLOR_INFO);
    if (g_state.temps.valid[TEMP_COLUMN_BOTTOM]) snprintf(buf, sizeof(buf), "%.1f", g_state.temps.columnBottom);
    else snprintf(buf, sizeof(buf), "—");
    hmi2Add(Hmi2RowType::Read, "Низ колонны", buf, "°C", nullptr,
            COLOR_INFO);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_state.power.power));
    hmi2Add(Hmi2RowType::Read, "Мощность", buf, "Вт", nullptr, COLOR_INFO);
    snprintf(buf, sizeof(buf), "%u",
             static_cast<unsigned>(g_state.pump.running ? g_state.pump.speedMlPerHour : 0));
    hmi2Add(Hmi2RowType::Read, "Отбор", buf, "мл/ч", nullptr, COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Параметры процесса", "", nullptr,
            "ОТКРЫТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Журнал", "", nullptr, "ОТКРЫТЬ",
            COLOR_INFO);
  } else if (hmi2Section == 6) { // СЕРВИС
    hmi2Add(Hmi2RowType::Link, "Все температуры", "", nullptr,
            "ОТКРЫТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Проверка оборудования", "", nullptr,
            "ОТКРЫТЬ", COLOR_WARNING);
    hmi2Add(Hmi2RowType::Link, "Дозирование", "", nullptr, "ОТКРЫТЬ",
            COLOR_PRIMARY);
    hmi2Add(Hmi2RowType::Link, "Журнал", "", nullptr, "ОТКРЫТЬ", COLOR_INFO);
    hmi2Add(Hmi2RowType::Link, "Калибровка тача", "", nullptr, "НАЧАТЬ",
            COLOR_WARNING);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    hmi2Add(Hmi2RowType::Read, "Свободная RAM", buf, "кБ", nullptr,
            COLOR_INFO);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(WiFi.RSSI()));
    hmi2Add(Hmi2RowType::Read, "RSSI", buf, "дБм", nullptr, COLOR_INFO);
    char build[16];
    FwBuildInfo::format(build, sizeof(build));
    snprintf(buf, sizeof(buf), "%s · %s", FW_VERSION, build);
    hmi2Add(Hmi2RowType::Read, "Версия", buf, nullptr, nullptr, COLOR_PRIMARY);
  } else {
    hmi2Add(Hmi2RowType::Notice, "Раздел в разработке",
            "строки появятся после переноса", nullptr, nullptr,
            COLOR_WARNING);
    hmi2Add(Hmi2RowType::Notice, "Макет",
            "проверяем размеры строк", nullptr, nullptr, COLOR_INFO);
  }
  return g_hmi2RowCount;
}

static bool hmi2ResolveValueField(Hmi2Field field,
                                  EditableFields::Id &descriptorId,
                                  float &value, ValueSaveCallback &save) {
  switch (field) {
  case Hmi2Field::HeaterPower:
    descriptorId = EditableFields::Id::EQUIPMENT_HEATER_POWER_W;
    value = g_settings.equipment.heaterPowerW;
    save = saveHeaterPower;
    return true;
  case Hmi2Field::ColumnHeight:
    descriptorId = EditableFields::Id::EQUIPMENT_COLUMN_HEIGHT_MM;
    value = g_settings.equipment.columnHeightMm;
    save = saveColumnHeight;
    return true;
  case Hmi2Field::CubeVolume:
    descriptorId = EditableFields::Id::EQUIPMENT_CUBE_VOLUME_L;
    value = g_settings.equipment.cubeVolumeL;
    save = saveCubeVolume;
    return true;
  case Hmi2Field::PackingCoeff:
    descriptorId = EditableFields::Id::EQUIPMENT_PACKING_COEFFICIENT;
    value = g_settings.equipment.packingCoeff;
    save = savePackingCoeff;
    return true;
  case Hmi2Field::MinHeaterSubmerge:
    descriptorId = EditableFields::Id::EQUIPMENT_MIN_HEATER_SUBMERGE_L;
    value = g_settings.equipment.minHeaterSubmergeL;
    save = saveMinHeaterSubmerge;
    return true;
  case Hmi2Field::BoosterPower:
    descriptorId = EditableFields::Id::EQUIPMENT_BOOSTER_HEATER_POWER_W;
    value = g_settings.equipment.boosterHeaterPowerW;
    save = saveBoosterHeaterPower;
    return true;
  case Hmi2Field::CoolingPwmHoldPercent:
    descriptorId = EditableFields::Id::COOLING_PWM_HOLD_PERCENT;
    value = g_settings.equipment.coolingPwmHoldPercent;
    save = saveCoolingPwmHoldPercent;
    return true;
  case Hmi2Field::CoolingPwmPullMs:
    descriptorId = EditableFields::Id::COOLING_PWM_PULL_MS;
    value = g_settings.equipment.coolingPwmPullMs;
    save = saveCoolingPwmPullMs;
    return true;
  case Hmi2Field::SafetyPressureMax:
    descriptorId = EditableFields::Id::SAFETY_PRESSURE_MAX_MMHG;
    value = g_settings.safety.pressureMaxMmHg;
    save = saveSafetyPressureMax;
    return true;
  case Hmi2Field::SafetyTsaMax:
    descriptorId = EditableFields::Id::SAFETY_TSA_MAX_C;
    value = g_settings.safety.tsaMaxC;
    save = saveSafetyTsaMax;
    return true;
  case Hmi2Field::SafetyWaterOutMax:
    descriptorId = EditableFields::Id::SAFETY_WATER_OUT_MAX_C;
    value = g_settings.safety.waterOutMaxC;
    save = saveSafetyWaterOutMax;
    return true;
  case Hmi2Field::SafetyWaterOutRiseRate:
    descriptorId = EditableFields::Id::SAFETY_WATER_OUT_RISE_RATE_C_MIN;
    value = g_settings.safety.waterOutRiseRateCMin;
    save = saveSafetyWaterOutRiseRate;
    return true;
  case Hmi2Field::SafetyPressureRiseRate:
    descriptorId = EditableFields::Id::SAFETY_PRESSURE_RISE_RATE_MMHG_MIN;
    value = g_settings.safety.pressureRiseRateMmHgMin;
    save = saveSafetyPressureRiseRate;
    return true;
  case Hmi2Field::None:
  case Hmi2Field::BoosterEnabled:
  case Hmi2Field::BodyLevelSensorEnabled:
  case Hmi2Field::LeakSensorEnabled:
  case Hmi2Field::Ds2482Enabled:
  case Hmi2Field::BrightnessPercent:
    descriptorId = EditableFields::Id::DISPLAY_BRIGHTNESS_PERCENT;
    value = static_cast<float>(g_settings.displaySettings.brightness) * 100.0f /
            255.0f;
    save = saveDisplayBrightness;
    return true;
  default:
    return false;
  }
}

static bool hmi2ToggleSetting(Hmi2Field field) {
  bool *setting = nullptr;
  bool refreshTemperatureInventory = false;
  switch (field) {
  case Hmi2Field::BoosterEnabled:
    setting = &g_settings.equipment.boosterHeaterEnabled;
    break;
  case Hmi2Field::BodyLevelSensorEnabled:
    setting = &g_settings.equipment.bodyLevelSensorEnabled;
    break;
  case Hmi2Field::LeakSensorEnabled:
    setting = &g_settings.equipment.leakSensorEnabled;
    break;
  case Hmi2Field::Ds2482Enabled:
    setting = &g_settings.equipment.useDs2482ForTemps;
    refreshTemperatureInventory = true;
    break;
  case Hmi2Field::DemoMode:
    setting = &g_settings.demoMode;
    break;
  case Hmi2Field::MqttEnabled:
    setting = &g_settings.mqtt.enabled;
    break;
  default:
    return false;
  }

  const bool previous = *setting;
  *setting = !previous;
  if (!NVSManager::saveSettings(g_settings)) {
    *setting = previous;
    return false;
  }
  if (refreshTemperatureInventory) {
    Sensors::refreshTemperatureInventory();
  }
  return true;
}

static bool hmi2FieldLocked(Hmi2Field field) {
  EditableFields::Id descriptorId;
  float value = 0.0f;
  ValueSaveCallback save = nullptr;
  if (!hmi2ResolveValueField(field, descriptorId, value, save)) return false;
  return isTftSetpointLocked(descriptorId);
}

static void hmi2OpenValueEditor(Hmi2Field field) {
  EditableFields::Id descriptorId;
  float value = 0.0f;
  ValueSaveCallback save = nullptr;
  if (hmi2ResolveValueField(field, descriptorId, value, save)) {
    openValueEdit(descriptorId, value, save);
  }
}

static bool hmi2AdjustValue(Hmi2Field field, int8_t direction, bool fast) {
  EditableFields::Id descriptorId;
  float value = 0.0f;
  ValueSaveCallback save = nullptr;
  if (!hmi2ResolveValueField(field, descriptorId, value, save)) {
    return false;
  }
  const EditableFields::Descriptor &descriptor =
      EditableFields::get(descriptorId);
  if (isTftSetpointLocked(descriptorId)) return false;
  const float step = fast ? descriptor.fastStep : descriptor.step;
  save(EditableFields::normalize(descriptorId, value + direction * step));
  requestRedraw(DisplayRedrawReason::TAP_ACTION);
  return true;
}

struct Hmi2RenderCache {
  bool valid = false;
  bool info = false;
  uint8_t section = 255;
  uint8_t scroll = 255;
  uint8_t total = 0;
  bool calibration = false;
  bool running = false;
  bool paused = false;
  bool dirty = false;
  Hmi2Row rows[HMI2_VISIBLE_ROWS];
};
static Hmi2RenderCache g_hmi2RenderCache;

static bool hmi2SameText(const char *a, const char *b) {
  return strcmp(a != nullptr ? a : "", b != nullptr ? b : "") == 0;
}

static bool hmi2SameRowFrame(const Hmi2Row &a, const Hmi2Row &b) {
  return a.type == b.type && a.field == b.field &&
         hmi2SameText(a.label, b.label) && hmi2SameText(a.unit, b.unit) &&
         hmi2SameText(a.linkText, b.linkText) && a.tone == b.tone;
}

static void hmi2DrawNavButton(int16_t x, bool left) {
  const int16_t y = HMI2_NAV_Y;
  const int16_t w = HMI2_NAV_W;
  const int16_t h = HMI2_NAV_H;
  const int16_t cx = x + w / 2;
  const int16_t cy = y + h / 2 - 3;
  tft.fillRect(x, y, w, h, colorButtonBody());
  tft.drawRect(x, y, w, h, colorBorder());
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_WHITE);
  const int16_t tip = left ? cx - 22 : cx + 22;
  const int16_t tail = left ? cx + 22 : cx - 22;
  const int16_t headX = left ? cx - 8 : cx + 8;
  const int16_t lineX = left ? tip : tail;
  const int16_t lineW = static_cast<int16_t>(abs(tail - tip) + 1);
  for (int16_t d = -1; d <= 1; ++d) {
    tft.drawFastHLine(lineX, cy + d, lineW, TFT_WHITE);
    tft.drawLine(tip, cy + d, headX, cy - 9 + d, TFT_WHITE);
    tft.drawLine(tip, cy + d, headX, cy + 9 + d, TFT_WHITE);
  }
}

static void hmi2DrawHeader(bool force = false) {
  static char previousClock[8] = {0};
  static uint8_t previousSection = 255;
  static uint8_t previousKind = 255;
  char currentClock[8];
  hmi2FormatClock(currentClock, sizeof(currentClock));
  const uint8_t kind = hmi2TempsPage ? 1 : (hmi2TouchCalibrationPage ? 2 : 0);
  const bool frameChanged = force || previousSection != hmi2Section || previousKind != kind;
  if (!frameChanged && strcmp(previousClock, currentClock) == 0) return;
  if (!frameChanged) {
    // The full header centers the clock at x=182. Partial updates must erase
    // and redraw the same rectangle; drawing at the old right-aligned x=445
    // leaves stale digits on the left after every minute change.
    tft.fillRect(158, 0, 48, HMI2_HEADER_H, colorNavBg());
    tft.setFont(&fonts::efontJA_16_b);
    tft.setTextSize(1);
    tft.setTextDatum(middle_center);
    tft.setTextColor(colorFg(), colorNavBg());
    drawDisplayString(currentClock, 182, HMI2_HEADER_H / 2);
    tft.setTextDatum(top_left);
    strcpy(previousClock, currentClock);
    return;
  }
  previousSection = hmi2Section;
  previousKind = kind;
  strcpy(previousClock, currentClock);
  tft.fillRect(0, 0, TFT_WIDTH, HMI2_HEADER_H, colorNavBg());
  tft.drawFastHLine(0, HMI2_HEADER_H - 1, TFT_WIDTH, colorBorder());

  const uint16_t accent = colorAccent();
  tft.setFont(&fonts::efontJA_16_b);
  tft.setTextSize(1);
  tft.drawRect(6, 8, 16, 16, accent);
  tft.fillRect(9, 11, 4, 4, accent);
  tft.fillRect(15, 11, 4, 4, accent);
  tft.fillRect(9, 17, 4, 4, accent);
  tft.fillRect(15, 17, 4, 4, accent);

  const char *subTitle = hmi2Subpage == Hmi2Subpage::Modes ? "ВЫБОР РЕЖИМА" :
      hmi2Subpage == Hmi2Subpage::Profiles ? "ВЫБОР ПРОФИЛЯ" :
      hmi2Subpage == Hmi2Subpage::Journal ? "ЖУРНАЛ" :
      hmi2Subpage == Hmi2Subpage::Wifi ? "WI-FI" :
      hmi2Subpage == Hmi2Subpage::WifiNetworks ? "СЕТИ WI-FI" :
      hmi2Subpage == Hmi2Subpage::WifiPassword ? "ПАРОЛЬ WI-FI" :
      hmi2Subpage == Hmi2Subpage::Sensors ? "ДАТЧИКИ: НАЗНАЧЕНИЕ" :
      hmi2Subpage == Hmi2Subpage::SensorAddresses ? "СВОБОДНЫЕ ДАТЧИКИ" :
      hmi2Subpage == Hmi2Subpage::SensorOffsets ? "КАЛИБР. ДАТЧИКОВ" : nullptr;
  const char *headerTitle = subTitle != nullptr && !hmi2TempsPage ? subTitle :
      hmi2TempsPage ? "ТАБЛО"
                    : (hmi2TouchCalibrationPage ? "КАЛИБРОВКА ТАЧА"
                                                : kHmi2Sections[hmi2Section]);
  const int16_t titleWidth = 120;
  char title[80];
  copyFittedText(headerTitle, titleWidth, title, sizeof(title));
  tft.setTextSize(1);
  tft.setTextColor(colorFg());
  tft.setTextDatum(middle_left);
  drawDisplayString(title, 28, HMI2_HEADER_H / 2);

  hmi2DrawNavButton(HMI2_NAV_LEFT_X, true);
  hmi2DrawNavButton(HMI2_NAV_RIGHT_X, false);

  char clock[8];
  hmi2FormatClock(clock, sizeof(clock));
  tft.setTextSize(1);
  tft.setTextColor(colorFg());
  tft.setTextDatum(middle_center);
  drawDisplayString(clock, 182, HMI2_HEADER_H / 2);
  tft.drawCircle(221, HMI2_HEADER_H / 2, 11, colorBorder());
  tft.fillCircle(221, HMI2_HEADER_H / 2, 7, COLOR_WARNING);
  tft.setTextDatum(top_left);
}

static LGFX_Sprite g_hmi2ValuePatch(&tft);
static LGFX_Sprite g_hmi2SmallPatch(&tft);

static void hmi2PaintValue(const char *text, int16_t x, int16_t y,
                          int16_t width, int16_t height, uint16_t fg,
                          uint16_t bg, bool small, float fontScale = 0.0f) {
  LGFX_Sprite &patch = small ? g_hmi2SmallPatch : g_hmi2ValuePatch;
  if (patch.width() != width || patch.height() != height) {
    patch.setPsram(true);
    patch.setColorDepth(16);
    patch.createSprite(width, height);
  }
  if (patch.getBuffer() != nullptr) {
    patch.fillSprite(bg);
    patch.setFont(&fonts::efontJA_16_b);
    patch.setTextSize(fontScale > 0 ? fontScale : (small ? 1.0f : 1.5f));
    patch.setTextDatum(middle_right);
    patch.setTextColor(fg, bg);
    patch.drawString(text, width - 3, height / 2);
    patch.pushSprite(&tft, x, y);
    return;
  }
  tft.setFont(&fonts::efontJA_16_b);
  tft.setTextSize(fontScale > 0 ? fontScale : (small ? 1.0f : 1.5f));
  tft.setTextDatum(middle_right);
  tft.setTextColor(fg, bg);
  tft.setTextPadding(width);
  drawDisplayString(text, x + width - 3, y + height / 2);
  tft.setTextPadding(0);
  tft.setTextDatum(top_left);
}

static void hmi2DrawRowValue(int16_t y, uint8_t index, const Hmi2Row &row) {
  const int16_t w = TFT_WIDTH - HMI2_RAIL_W;
  const bool read = row.type == Hmi2RowType::Read;
  const int16_t right = w - 10 - (read ? 0 : 96);
  char text[96];
  snprintf(text, sizeof(text), "%s%s%s", row.value,
           row.unit[0] ? " " : "", row.unit);
  const int16_t zoneWidth = read ? 220 : 120;
  hmi2PaintValue(text, right - zoneWidth, y + 2, zoneWidth, 56, row.tone,
                 (index & 1) ? colorCard() : colorNavBg(), false);
}

static bool hmi2FieldLocked(Hmi2Field field);

static void hmi2DrawRow(int16_t y, uint8_t index, const Hmi2Row &row) {
  const int16_t x = HMI2_MARGIN_X;
  const int16_t w = TFT_WIDTH - 2 * HMI2_MARGIN_X - HMI2_RAIL_W;
  const int16_t h = HMI2_ROW_H - HMI2_ROW_GAP;
  const bool settingReadOnly =
      (row.type == Hmi2RowType::Value || row.type == Hmi2RowType::Bool) &&
      (!isScreenEditable(UI_EQUIPMENT, g_state) ||
       (row.field != Hmi2Field::None &&
        hmi2FieldLocked(row.field)));
  const uint16_t rowTone = settingReadOnly ? colorMuted() : row.tone;

  tft.setFont(&fonts::efontJA_16_b);
  tft.setTextSize(1.5f);

  // Чередование фона — как в макете, чтобы строки читались по отдельности.
  tft.fillRect(x, y, w, h, (index & 1) ? colorCard() : colorNavBg());
  tft.drawFastHLine(x, y + h - 1, w, colorBorder());

  if (row.type == Hmi2RowType::Notice) {
    tft.setTextSize(1);
    tft.setTextColor(row.tone);
    tft.setTextDatum(middle_left);
    drawDisplayString("!", x + 10, y + 17);
    tft.setTextSize(1.5f);
    tft.setTextColor(colorFg());
    drawDisplayString(row.label, x + 28, y + 17);
    tft.setTextColor(colorMuted());
    drawDisplayString(row.value, x + 28, y + 43);
    tft.setTextDatum(top_left);
    tft.setTextSize(1);
    return;
  }

  int16_t rightEdge = x + w - 10;
  if (row.type == Hmi2RowType::Link && row.linkText != nullptr) {
    const int16_t bw = 104;
    const int16_t bh = 40;
    tft.setTextSize(1);
    drawButton(rightEdge - bw, y + (h - bh) / 2, bw, bh, row.linkText, row.tone,
               TFT_WHITE);
    tft.setTextSize(1.5f);
    rightEdge -= (bw + 8);
  } else if (row.type == Hmi2RowType::Bool) {
    const int16_t bs = 30;
    const int16_t bx = rightEdge - bs;
    const int16_t by = y + (h - bs) / 2;
    tft.drawRoundRect(bx, by, bs, bs, 5,
                      settingReadOnly ? colorMuted() : colorBorder());
    if (row.value[0] == '1') {
      tft.fillRoundRect(bx + 5, by + 5, bs - 10, bs - 10, 3, rowTone);
    }
    rightEdge -= (bs + 8);
  }

  if (row.type == Hmi2RowType::Value || row.type == Hmi2RowType::Read) {
    // Read — только значение (главный экран), Value — значение со степперами.
    if (row.type == Hmi2RowType::Value) {
      const int16_t stepW = 44;
      const int16_t stepH = 40;
      const int16_t stepY = y + (h - stepH) / 2;
      const uint16_t stepTone =
          settingReadOnly ? colorNavInactive() : colorButtonBody();
      const uint16_t stepText = settingReadOnly ? colorMuted() : colorFg();
      tft.setTextSize(1);
      drawButton(rightEdge - stepW, stepY, stepW, stepH, "+",
                 stepTone, stepText);
      drawButton(rightEdge - 2 * stepW - 4, stepY, stepW, stepH, "-",
                 stepTone, stepText);
      tft.setTextSize(1.5f);
      rightEdge -= (2 * stepW + 8);
    }

    const int16_t zoneWidth = row.type == Hmi2RowType::Read ? 220 : 120;
    char displayValue[96];
    snprintf(displayValue, sizeof(displayValue), "%s%s%s", row.value,
             row.unit[0] ? " " : "", row.unit);
    hmi2PaintValue(displayValue, rightEdge - zoneWidth, y + 2, zoneWidth, 56,
                   rowTone, (index & 1) ? colorCard() : colorNavBg(), false);
    rightEdge -= zoneWidth + 8;
    /* Units and value share one opaque patch to avoid intermediate blanking.
    if (row.unit[0] != '\0') {
      tft.setTextSize(1.5f);
      tft.setTextColor(colorMuted());
      tft.setTextDatum(middle_right);
      drawDisplayString(row.unit, rightEdge, y + h / 2);
      rightEdge -= (displayTextWidth(row.unit) + 8);
    }

    tft.setTextSize(1.5f);
    tft.setTextColor(rowTone);
    tft.setTextDatum(middle_right);
    drawDisplayString(row.value, rightEdge, y + h / 2);
    rightEdge -= (displayTextWidth(row.value) + 12);
    */
    tft.setTextDatum(top_left);
  }

  char label[24];
  const int16_t labelWidth = rightEdge - x - 16;
  copyFittedText(row.label, labelWidth, label, sizeof(label));
  tft.setTextSize(1.5f);
  tft.setTextColor(colorFg());
  tft.setTextDatum(middle_left);
  if (row.type == Hmi2RowType::Link && row.value[0] != '\0') {
    drawDisplayString(label, x + 10, y + h / 2 - 13);
    char detail[24];
    copyFittedText(row.value, labelWidth, detail, sizeof(detail));
    tft.setTextColor(colorMuted());
    drawDisplayString(detail, x + 10, y + h / 2 + 13);
  } else {
    drawDisplayString(label, x + 10, y + h / 2);
  }
  tft.setTextDatum(top_left);
  tft.setTextSize(1);
}

static void hmi2DrawRail(uint8_t total) {
  const int16_t x = TFT_WIDTH - HMI2_RAIL_W + 3;
  const int16_t top = HMI2_HEADER_H + 3;
  const int16_t height = HMI2_VISIBLE_ROWS * HMI2_ROW_H - 6;
  tft.fillRect(TFT_WIDTH - HMI2_RAIL_W, HMI2_HEADER_H, HMI2_RAIL_W,
               HMI2_VISIBLE_ROWS * HMI2_ROW_H, colorNavBg());
  drawButton(x, top, 42, 37, "▲", colorButtonBody(), colorFg());
  drawButton(x, TFT_HEIGHT - HMI2_FOOTER_H - 42, 42, 37, "▼",
             colorButtonBody(), colorFg());
  const int16_t trackTop = top + 41;
  const int16_t trackBottom = TFT_HEIGHT - HMI2_FOOTER_H - 54;
  const int16_t trackH = trackBottom - trackTop;
  tft.fillRoundRect(TFT_WIDTH - HMI2_RAIL_W + 21, trackTop, 7, trackH, 4,
                    colorButtonBody());
  if (total <= HMI2_VISIBLE_ROWS) {
    tft.fillRoundRect(TFT_WIDTH - HMI2_RAIL_W + 21, trackTop, 7, trackH, 4,
                      colorAccent());
  } else {
    const uint8_t maxFirst = static_cast<uint8_t>(total - HMI2_VISIBLE_ROWS);
    const int16_t thumbH = trackH * HMI2_VISIBLE_ROWS / total;
    const int16_t travel = trackH - thumbH;
    const int16_t off =
        (maxFirst == 0) ? 0
                        : travel * static_cast<int16_t>(hmi2Scroll) / maxFirst;
    tft.fillRoundRect(TFT_WIDTH - HMI2_RAIL_W + 21, trackTop + off, 7, thumbH,
                      4, colorAccent());
  }
  char counter[12];
  snprintf(counter, sizeof(counter), "%u-%u/%u",
           static_cast<unsigned>(hmi2Scroll + 1),
           static_cast<unsigned>(hmi2Scroll + HMI2_VISIBLE_ROWS > total
                                     ? total
                                     : hmi2Scroll + HMI2_VISIBLE_ROWS),
           static_cast<unsigned>(total));
  tft.setTextSize(1);
  tft.setTextColor(colorFg());
  tft.setTextDatum(middle_center);
  drawDisplayString(counter, TFT_WIDTH - HMI2_RAIL_W / 2,
                    TFT_HEIGHT - HMI2_FOOTER_H - 52);
  tft.setTextDatum(top_left);
}

static void hmi2DrawFooter(bool running, bool paused) {
  const int16_t y = TFT_HEIGHT - HMI2_FOOTER_H;
  tft.fillRect(0, y, TFT_WIDTH, HMI2_FOOTER_H, colorNavBg());
  tft.drawFastHLine(0, y, TFT_WIDTH, colorBorder());
  const int16_t bh = 38;
  const int16_t by = y + (HMI2_FOOTER_H - bh) / 2;
  if (running) {
    drawButton(6, by, 152, bh, paused ? "ПРОДОЛЖИТЬ" : "ПАУЗА",
               colorButtonBody(), colorFg());
    drawButton(164, by, 152, bh, "СТОП", COLOR_DANGER, TFT_WHITE);
    drawButton(322, by, 152, bh, "СЕРВИС", colorButtonBody(), colorFg());
    return;
  }
  if (ui.currentScreen == UI_MODE_SETUP && hasUnsavedModeSetupDraft()) {
    drawButton(6, by, 152, bh, "НАЗАД", colorButtonBody(), colorFg());
    drawButton(164, by, 152, bh, "ВЫХОД", colorButtonBody(), colorFg());
    drawButton(322, by, 152, bh, "СОХРАНИТЬ", COLOR_SUCCESS, TFT_WHITE);
    return;
  }
  if (hmi2Subpage != Hmi2Subpage::None) {
    drawButton(6, by, 152, bh, "НАЗАД", colorButtonBody(), colorFg());
    drawButton(164, by, 152, bh, "ВЫХОД", colorButtonBody(), colorFg());
    drawButton(322, by, 152, bh, "СОХРАНИТЬ", COLOR_DARK_GREY, colorMuted());
    return;
  }
  if (hmi2TouchCalibrationPage) {
    drawButton(6, by, 152, bh, "НАЗАД", colorButtonBody(), colorFg());
    drawButton(164, by, 152, bh, "ГЛАВНАЯ", colorButtonBody(), colorFg());
    drawButton(322, by, 152, bh, "СОХРАНИТЬ", COLOR_DARK_GREY, colorMuted());
    return;
  }
  drawButton(6, by, 152, bh, "НАЗАД", colorButtonBody(), colorFg());
  drawButton(164, by, 152, bh, "ВЫХОД", colorButtonBody(), colorFg());
  drawButton(322, by, 152, bh, "СОХРАНИТЬ", COLOR_DARK_GREY, colorMuted());
}

struct Hmi2InfoPair {
  const char *key;
  char value[40];
  bool dim;
};

static void hmi2FormatInfoTemp(char *out, size_t size, uint8_t index) {
  if (!g_state.temps.valid[index]) {
    snprintf(out, size, "—");
    return;
  }
  float value = 0.0f;
  switch (index) {
  case TEMP_CUBE: value = g_state.temps.cube; break;
  case TEMP_COLUMN_BOTTOM: value = g_state.temps.columnBottom; break;
  case TEMP_COLUMN_TOP: value = g_state.temps.columnTop; break;
  case TEMP_REFLUX: value = g_state.temps.deflegmator; break;
  case TEMP_TSA: value = g_state.temps.tsa; break;
  case TEMP_WATER_IN: value = g_state.temps.waterIn; break;
  case TEMP_WATER_OUT: value = g_state.temps.waterOut; break;
  default: break;
  }
  snprintf(out, size, "%.1f °C", static_cast<double>(value));
  for (char *c = out; *c; ++c) {
    if (*c == '.') *c = ',';
  }
}

// "ТАБЛО" from the HMI2 prototype: two columns, six rows, eleven parameters
// (seven DS18B20 channels + cube pressure, atmosphere, takeoff, power).
static uint8_t hmi2BuildInfoPairs(Hmi2InfoPair *out, uint8_t capacity) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < TEMP_COUNT && count < capacity; ++i) {
    out[count].key = getTempSensorLabel(i);
    hmi2FormatInfoTemp(out[count].value, sizeof(out[count].value), i);
    out[count].dim = !g_state.temps.valid[i];
    ++count;
  }
  if (count < capacity) {
    Hmi2InfoPair &pair = out[count++];
    pair.key = "Давл. куб";
    pair.dim = !g_state.pressure.ok;
    if (pair.dim) {
      snprintf(pair.value, sizeof(pair.value), "—");
    } else {
      snprintf(pair.value, sizeof(pair.value), "%.0f мм рт.ст.",
               static_cast<double>(g_state.pressure.cube));
    }
  }
  if (count < capacity) {
    Hmi2InfoPair &pair = out[count++];
    pair.key = "Атмосфера";
    pair.dim = false;
    snprintf(pair.value, sizeof(pair.value), "%.0f мм",
             static_cast<double>(g_state.pressure.atmosphere * 0.750062f));
  }
  if (count < capacity) {
    Hmi2InfoPair &pair = out[count++];
    pair.key = "Отбор";
    pair.dim = false;
    snprintf(pair.value, sizeof(pair.value), "%.0f мл/ч",
             static_cast<double>(g_state.pump.running
                                     ? g_state.pump.speedMlPerHour
                                     : 0.0f));
  }
  if (count < capacity) {
    Hmi2InfoPair &pair = out[count++];
    pair.key = "Мощность";
    pair.dim = false;
    snprintf(pair.value, sizeof(pair.value), "%.0f Вт",
             static_cast<double>(g_state.power.power));
  }
  return count;
}

static void hmi2DrawInfoFooter(bool running) {
  const int16_t y = TFT_HEIGHT - HMI2_FOOTER_H;
  tft.fillRect(0, y, TFT_WIDTH, HMI2_FOOTER_H, colorNavBg());
  tft.drawFastHLine(0, y, TFT_WIDTH, colorBorder());
  const int16_t bh = 38;
  const int16_t by = y + (HMI2_FOOTER_H - bh) / 2;
  drawButton(6, by, 152, bh, "НАЗАД", colorButtonBody(), colorFg());
  drawButton(322, by, 152, bh, "СТОП",
             running ? COLOR_DANGER : COLOR_DARK_GREY,
             running ? TFT_WHITE : colorMuted());
}

static Hmi2InfoPair g_hmi2InfoCache[12];

static void hmi2DrawInfoCell(uint8_t index, const Hmi2InfoPair &pair,
                            uint8_t rows, bool frame) {
  const int16_t rowH = (TFT_HEIGHT - HMI2_FOOTER_H - HMI2_HEADER_H - 4) / rows;
  const uint8_t fontPx = HmiPage::fontForRows(rows,
      TFT_HEIGHT - HMI2_FOOTER_H - HMI2_HEADER_H - 4, 12, 20);
  const float keyScale = static_cast<float>(fontPx) / 16.0f;
  float valueScale = static_cast<float>(fontPx + 2) / 16.0f;
  const int16_t x = (index / rows) * (TFT_WIDTH / 2);
  const int16_t y = HMI2_HEADER_H + 2 + (index % rows) * rowH;
  if (frame) {
    tft.setFont(&fonts::efontJA_16);
    tft.setTextSize(keyScale);
    tft.setTextDatum(middle_left);
    tft.setTextColor(TFT_BLACK);
    drawDisplayString(pair.key, x + 6, y + rowH / 2);
    tft.drawFastHLine(x + 4, y + rowH - 1, TFT_WIDTH / 2 - 8, colorBorder());
  }
  tft.setFont(&fonts::efontJA_16_b);
  while (valueScale > 1.0f) {
    tft.setTextSize(valueScale);
    if (displayTextWidth(pair.value) <= 112) break;
    valueScale -= 0.0625f;
  }
  hmi2PaintValue(pair.value, x + 116, y + 2, 116, rowH - 4,
                 pair.dim ? tft.color565(90, 90, 90) : TFT_BLACK, TFT_WHITE,
                 true, valueScale);
}

static void hmi2RenderTemps(bool full) {
  Hmi2InfoPair pairs[12];
  const uint8_t count = hmi2BuildInfoPairs(pairs, 12);
  const uint8_t rows = (count + 1) / 2;
  const bool layout = full || !g_hmi2RenderCache.valid || !g_hmi2RenderCache.info ||
                      g_hmi2RenderCache.total != count;
  const bool running = isModeRunning(g_state);
  if (layout) {
    tft.fillScreen(colorBg());
    tft.fillRect(0, HMI2_HEADER_H, TFT_WIDTH,
                 TFT_HEIGHT - HMI2_HEADER_H - HMI2_FOOTER_H, TFT_WHITE);
  }
  hmi2DrawHeader(layout);
  for (uint8_t i = 0; i < count; ++i) {
    if (layout || strcmp(pairs[i].value, g_hmi2InfoCache[i].value) != 0 ||
        pairs[i].dim != g_hmi2InfoCache[i].dim) {
      hmi2DrawInfoCell(i, pairs[i], rows, layout);
      g_hmi2InfoCache[i] = pairs[i];
    }
  }
  if (layout || g_hmi2RenderCache.running != running) hmi2DrawInfoFooter(running);
  g_hmi2RenderCache.valid = true;
  g_hmi2RenderCache.info = true;
  g_hmi2RenderCache.total = count;
  g_hmi2RenderCache.running = running;
}

static void hmi2RenderHome(bool full) {
  const uint8_t total = hmi2BuildRows();
  const uint8_t maxFirst = total > HMI2_VISIBLE_ROWS ? total - HMI2_VISIBLE_ROWS : 0;
  if (hmi2Scroll > maxFirst) hmi2Scroll = maxFirst;
  const bool layout = full || !g_hmi2RenderCache.valid || g_hmi2RenderCache.info ||
      g_hmi2RenderCache.section != hmi2Section ||
      g_hmi2RenderCache.scroll != hmi2Scroll ||
      g_hmi2RenderCache.total != total ||
      g_hmi2RenderCache.calibration != hmi2TouchCalibrationPage;
  const bool running = isModeRunning(g_state);
  if (layout) tft.fillScreen(colorBg());
  hmi2DrawHeader(layout);
  for (uint8_t slot = 0; slot < HMI2_VISIBLE_ROWS; ++slot) {
    const uint8_t index = hmi2Scroll + slot;
    if (index >= total) continue;
    const Hmi2Row &row = g_hmi2Rows[index];
    const Hmi2Row &previous = g_hmi2RenderCache.rows[slot];
    const int16_t y = HMI2_HEADER_H + slot * HMI2_ROW_H;
    if (layout || !hmi2SameRowFrame(row, previous)) {
      hmi2DrawRow(y, index, row);
    } else if (strcmp(row.value, previous.value) != 0) {
      if (row.type == Hmi2RowType::Read || row.type == Hmi2RowType::Value)
        hmi2DrawRowValue(y, index, row);
      else
        hmi2DrawRow(y, index, row);
    }
    g_hmi2RenderCache.rows[slot] = row;
  }
  if (layout) hmi2DrawRail(total);
  if (layout || g_hmi2RenderCache.running != running ||
      g_hmi2RenderCache.paused != g_state.paused ||
      g_hmi2RenderCache.dirty != hasUnsavedModeSetupDraft())
    hmi2DrawFooter(running, g_state.paused);
  g_hmi2RenderCache.valid = true;
  g_hmi2RenderCache.info = false;
  g_hmi2RenderCache.section = hmi2Section;
  g_hmi2RenderCache.scroll = hmi2Scroll;
  g_hmi2RenderCache.total = total;
  g_hmi2RenderCache.calibration = hmi2TouchCalibrationPage;
  g_hmi2RenderCache.running = running;
  g_hmi2RenderCache.paused = g_state.paused;
  g_hmi2RenderCache.dirty = hasUnsavedModeSetupDraft();
}

static void hmi2RenderEditor(bool full) {
  static const EditableFields::Descriptor *previousDescriptor = nullptr;
  static float previousValue = NAN;
  if (edit.descriptor == nullptr) return;
  const bool layout = full || previousDescriptor != edit.descriptor;
  if (layout) {
    tft.fillScreen(colorBg());
    tft.fillRect(0, 0, TFT_WIDTH, HMI2_HEADER_H, colorNavBg());
    tft.setFont(&fonts::efontJA_16_b);
    tft.setTextSize(1);
    tft.setTextColor(colorFg());
    tft.setTextDatum(middle_left);
    char label[80];
    copyFittedText(EditableFields::label(edit.descriptor->id, true), TFT_WIDTH - 20, label, sizeof(label));
    drawDisplayString(label, 10, HMI2_HEADER_H / 2);
    drawButton(6, 150, 114, 56, "--", COLOR_DANGER, TFT_WHITE);
    drawButton(123, 150, 114, 56, "-", colorButtonBody(), TFT_WHITE);
    drawButton(240, 150, 114, 56, "+", colorButtonBody(), TFT_WHITE);
    drawButton(357, 150, 114, 56, "++", COLOR_SUCCESS, TFT_WHITE);
    const bool locked = isTftSetpointLocked(edit.descriptor->id);
    drawButton(6, 277, 228, 38, "ОТМЕНА", colorButtonBody(), TFT_WHITE);
    drawButton(246, 277, 228, 38, "СОХРАНИТЬ",
               locked ? COLOR_DARK_GREY : COLOR_SUCCESS,
               locked ? colorMuted() : TFT_WHITE);
    tft.setTextColor(locked ? COLOR_WARNING : colorMuted());
    tft.setTextDatum(middle_center);
    drawDisplayString(locked ? "Выход активен: сначала СТОП, затем уставка"
                             : "Изменения применяются только после сохранения",
                      TFT_WIDTH / 2, 244);
  }
  if (layout || edit.value != previousValue) {
    char value[64];
    snprintf(value, sizeof(value), "%.*f %s", edit.descriptor->decimals, static_cast<double>(edit.value), edit.descriptor->unit);
    hmi2PaintValue(value, 120, 66, 240, 56, COLOR_INFO, colorBg(), false);
    previousValue = edit.value;
  }
  previousDescriptor = edit.descriptor;
}

static void hmi2DrawWifiKeyboard(bool full) {
  static HmiKeyboard::Layout previousLayout = HmiKeyboard::Layout::Letters;
  static bool previousShift = false;
  static uint8_t previousLength = 255;
  const bool keysChanged = full || previousLayout != hmi2WifiKeyboard.layout ||
                           previousShift != hmi2WifiKeyboard.shift;
  if (full) {
    tft.fillScreen(colorBg());
    hmi2DrawHeader(true);
    tft.drawRect(HmiKeyboard::kFieldX, HmiKeyboard::kFieldY,
                 HmiKeyboard::kFieldW, HmiKeyboard::kFieldH, colorBorder());
    tft.setFont(&fonts::efontJA_16_b);
    tft.setTextSize(0.875f);
    tft.setTextColor(colorFg());
    tft.setTextDatum(top_left);
    char caption[80];
    copyFittedText(hmi2WifiSsid, 350, caption, sizeof(caption));
    drawDisplayString(caption, HmiKeyboard::kCaptionX, HmiKeyboard::kCaptionY);
    const HmiKeyboard::Rect cancel = HmiKeyboard::footerRect(false);
    const HmiKeyboard::Rect done = HmiKeyboard::footerRect(true);
    drawButton(cancel.x, cancel.y + 5, cancel.w, cancel.h - 10,
               "ОТМЕНА", colorButtonBody(), TFT_WHITE);
    drawButton(done.x, done.y + 5, done.w, done.h - 10,
               "ГОТОВО", COLOR_SUCCESS, TFT_WHITE);
  } else {
    hmi2DrawHeader(false);
  }
  if (full || previousLength != hmi2WifiKeyboard.length) {
    char masked[64], counter[16];
    HmiKeyboard::masked(hmi2WifiKeyboard, masked, sizeof(masked));
    HmiKeyboard::counter(hmi2WifiKeyboard, counter, sizeof(counter));
    // Show only the tail of the mask; never reveal a saved password.
    const char *tail = masked + (hmi2WifiKeyboard.length > 36 ?
                                hmi2WifiKeyboard.length - 36 : 0);
    hmi2PaintValue(tail, HmiKeyboard::kFieldX + 1, HmiKeyboard::kFieldY + 1,
                   HmiKeyboard::kFieldW - 2, HmiKeyboard::kFieldH - 2,
                   TFT_BLACK, TFT_WHITE, true);
    hmi2PaintValue(counter, 380, 77, 92, 18, colorFg(), colorBg(), true, 0.875f);
  }
  if (keysChanged) {
    tft.fillRect(0, HmiKeyboard::kKeysY, TFT_WIDTH,
                 HmiKeyboard::kFooterY - HmiKeyboard::kKeysY, colorBg());
    for (uint8_t row = 0; row < 4; ++row) {
      const uint8_t length = HmiKeyboard::rowLength(hmi2WifiKeyboard.layout, row);
      for (uint8_t index = 0; index < length; ++index) {
        const HmiKeyboard::Rect rect = HmiKeyboard::keyRect(
            hmi2WifiKeyboard.layout, row, index);
        char ch[2] = {0, 0};
        const char *label;
        if (row < 3) {
          ch[0] = HmiKeyboard::displayChar(hmi2WifiKeyboard,
              HmiKeyboard::rowText(hmi2WifiKeyboard.layout, row)[index]);
          label = ch;
        } else {
          label = HmiKeyboard::functionLabel(index, hmi2WifiKeyboard.layout);
        }
        drawButton(rect.x, rect.y, rect.w, rect.h, label,
                   row == 3 && index == 0 && hmi2WifiKeyboard.shift ?
                       COLOR_INFO : colorButtonBody(), TFT_WHITE);
      }
    }
  }
  previousLayout = hmi2WifiKeyboard.layout;
  previousShift = hmi2WifiKeyboard.shift;
  previousLength = hmi2WifiKeyboard.length;
}

static void hmi2RenderRoot(const SystemState &state, bool full) {
  if (hmi2LastScreen != ui.currentScreen) {
    // Returning from the numeric editor must keep the subpage it was opened
    // from (sensor offsets, Wi-Fi password); otherwise the operator lands on
    // the section root and has to navigate again for every value.
    const bool returningFromEditor = hmi2LastScreen == UI_VALUE_EDIT;
    hmi2LastScreen = ui.currentScreen;
    if (!returningFromEditor) {
      hmi2Scroll = 0;
      hmi2Subpage = Hmi2Subpage::None;
    }
    if (!returningFromEditor) {
      if (ui.currentScreen == UI_MODE_MONITOR || ui.currentScreen == UI_CONTROL) hmi2Section = 7;
      else if (ui.currentScreen == UI_SETTINGS) hmi2Section = 2;
      else if (ui.currentScreen == UI_SERVICE) hmi2Section = 6;
      else if (ui.currentScreen == UI_MODE_SETUP) hmi2Section = 1;
      else if (ui.currentScreen == UI_MODE_PICKER) hmi2Subpage = Hmi2Subpage::Modes;
      else if (ui.currentScreen == UI_PROFILE_PICKER) hmi2Subpage = Hmi2Subpage::Profiles;
      else if (ui.currentScreen == UI_PROCESS_EVENTS) hmi2Subpage = Hmi2Subpage::Journal;
      else if (ui.currentScreen == UI_ALL_TEMPS) hmi2TempsPage = true;
    }
    g_hmi2RenderCache.valid = false;
    full = true;
  }
  if (ui.currentScreen == UI_VALUE_EDIT) { hmi2RenderEditor(full); return; }
  if (hmi2Subpage == Hmi2Subpage::WifiPassword) { hmi2DrawWifiKeyboard(full); return; }
  if (hmi2TempsPage) hmi2RenderTemps(full);
  else hmi2RenderHome(full);
}

static bool hmi2HandleHomeTap(int16_t tx, int16_t ty,
                              const SystemState &state) {
  if (HMI_LEGACY) return false;
  if (hmi2Subpage == Hmi2Subpage::WifiPassword) {
    HmiKeyboard::Result result = HmiKeyboard::applyAt(hmi2WifiKeyboard, tx, ty);
    if (ty < HMI2_HEADER_H &&
        (hit(tx, ty, HMI2_NAV_LEFT_X, HMI2_NAV_Y, HMI2_NAV_W, HMI2_NAV_H) ||
         hit(tx, ty, HMI2_NAV_RIGHT_X, HMI2_NAV_Y, HMI2_NAV_W, HMI2_NAV_H))) {
      HmiKeyboard::Key cancel;
      cancel.action = HmiKeyboard::Action::Cancel;
      result = HmiKeyboard::applyKey(hmi2WifiKeyboard, cancel);
    }
    if (result == HmiKeyboard::Result::Cancelled) {
      hmi2Subpage = Hmi2Subpage::Wifi;
      requestFullScreenRedraw();
      armScreenSwitchGuard();
    } else if (result == HmiKeyboard::Result::Committed) {
      hmi2SaveWifiPassword();
    } else if (result == HmiKeyboard::Result::Changed) {
      requestRedraw(DisplayRedrawReason::TAP_ACTION);
    }
    return true;
  }
  if (ui.currentScreen == UI_VALUE_EDIT) {
    if (edit.descriptor == nullptr) return true;
    if (isTftSetpointLocked(edit.descriptor->id)) {
      if (hit(tx, ty, 6, 277, 228, 38)) popScreen();
      return true;
    }
    if (ty >= 150 && ty < 206) {
      const int8_t direction = tx < 237 ? -1 : 1;
      const bool fast = tx < 120 || tx >= 357;
      edit.value = EditableFields::normalize(edit.descriptor->id,
          edit.value + direction * (fast ? edit.descriptor->fastStep : edit.descriptor->step));
      requestRedraw(DisplayRedrawReason::TAP_ACTION);
    } else if (hit(tx, ty, 6, 277, 228, 38)) {
      popScreen();
    } else if (hit(tx, ty, 246, 277, 228, 38)) {
      if (edit.onSave) edit.onSave(EditableFields::normalize(edit.descriptor->id, edit.value));
      popScreen();
    }
    return true;
  }
  if (hmi2TempsPage) {
    if (hit(tx, ty, HMI2_NAV_LEFT_X, HMI2_NAV_Y, HMI2_NAV_W, HMI2_NAV_H)) {
      hmi2TempsPage = false;
      hmi2Section = hmi2TempsReturnSection;
      hmi2Scroll = hmi2TempsReturnScroll;
      if (ui.currentScreen == UI_ALL_TEMPS) popScreen();
      requestFullScreenRedraw();
      return true;
    }
    if (ty >= TFT_HEIGHT - HMI2_FOOTER_H) {
      const int16_t by = TFT_HEIGHT - HMI2_FOOTER_H + 5;
      if (hit(tx, ty, 6, by, 152, 38)) {
        hmi2TempsPage = false;
        hmi2Section = hmi2TempsReturnSection;
      hmi2Scroll = hmi2TempsReturnScroll;
      if (ui.currentScreen == UI_ALL_TEMPS) popScreen();
        requestFullScreenRedraw();
        return true;
      }
      if (isModeRunning(state) && hit(tx, ty, 322, by, 152, 38)) {
        openOperatorOverlay(TftOperatorOverlayLogic::Action::ProcessStop);
        requestFullScreenRedraw();
        return true;
      }
      return true;
    }
    return true;
  }
  if (!hmi2TouchCalibrationPage && hit(tx, ty, 0, 0, 28, HMI2_HEADER_H)) {
    hmi2TempsReturnSection = hmi2Section;
    hmi2TempsReturnScroll = hmi2Scroll;
    hmi2TempsPage = true;
    hmi2Scroll = 0;
    requestFullScreenRedraw();
    return true;
  }
  if (hit(tx, ty, HMI2_NAV_LEFT_X, HMI2_NAV_Y, HMI2_NAV_W, HMI2_NAV_H) ||
      (hmi2Subpage != Hmi2Subpage::None &&
       hit(tx, ty, 6, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38))) {
    if (hmi2TempsPage) {
      hmi2TempsPage = false;
      hmi2Section = hmi2TempsReturnSection;
      hmi2Scroll = hmi2TempsReturnScroll;
      if (ui.currentScreen == UI_ALL_TEMPS) popScreen();
    } else if (hmi2TouchCalibrationPage) {
      hmi2TouchCalibrationPage = false;
      hmi2TouchCalibrationMessage[0] = '\0';
      hmi2Scroll = 0;
    } else if (hmi2Subpage == Hmi2Subpage::WifiNetworks) {
      hmi2Subpage = Hmi2Subpage::Wifi;
      hmi2Scroll = 0;
    } else if (hmi2Subpage == Hmi2Subpage::Sensors ||
               hmi2Subpage == Hmi2Subpage::SensorAddresses ||
               hmi2Subpage == Hmi2Subpage::SensorOffsets) {
      hmi2Subpage = Hmi2Subpage::None;
      hmi2Scroll = 0;
    } else if (hmi2Subpage != Hmi2Subpage::None) {
      if (ui.currentScreen == UI_PROFILE_PICKER || ui.currentScreen == UI_PROCESS_EVENTS ||
          ui.currentScreen == UI_MODE_PICKER) popScreen();
      hmi2Subpage = Hmi2Subpage::None;
      hmi2Scroll = 0;
    } else if (hmi2Section == 0) {
      hmi2TempsReturnSection = hmi2Section;
      hmi2TempsReturnScroll = hmi2Scroll;
      hmi2TempsPage = true;
      hmi2Scroll = 0;
    } else {
      hmi2Section = static_cast<uint8_t>(
          (hmi2Section + kHmi2SectionCount - 1) % kHmi2SectionCount);
      hmi2Scroll = 0;
    }
    requestFullScreenRedraw();
    armScreenSwitchGuard();
    return true;
  }
  if (hit(tx, ty, HMI2_NAV_RIGHT_X, HMI2_NAV_Y, HMI2_NAV_W, HMI2_NAV_H)) {
    if (hmi2TempsPage) {
      hmi2TempsPage = false;
      hmi2Section = hmi2TempsReturnSection;
      hmi2Scroll = hmi2TempsReturnScroll;
      if (ui.currentScreen == UI_ALL_TEMPS) popScreen();
    } else if (hmi2Subpage != Hmi2Subpage::None) {
      if (ui.currentScreen == UI_PROFILE_PICKER || ui.currentScreen == UI_PROCESS_EVENTS ||
          ui.currentScreen == UI_MODE_PICKER) popScreen();
      hmi2Subpage = Hmi2Subpage::None;
    } else {
      hmi2Section = static_cast<uint8_t>((hmi2Section + 1) % kHmi2SectionCount);
      hmi2Scroll = 0;
    }
    requestFullScreenRedraw();
    armScreenSwitchGuard();
    return true;
  }
  const uint8_t total = hmi2BuildRows();
  if (tx >= TFT_WIDTH - HMI2_RAIL_W &&
      ty >= HMI2_HEADER_H &&
      ty < HMI2_HEADER_H + HMI2_VISIBLE_ROWS * HMI2_ROW_H) {
    const uint8_t maxFirst =
        (total > HMI2_VISIBLE_ROWS)
            ? static_cast<uint8_t>(total - HMI2_VISIBLE_ROWS)
            : static_cast<uint8_t>(0);
    const uint8_t previousScroll = hmi2Scroll;
    if (hit(tx, ty, TFT_WIDTH - HMI2_RAIL_W + 3, HMI2_HEADER_H + 3, 42, 37)) {
      hmi2Scroll = (hmi2Scroll > 0) ? static_cast<uint8_t>(hmi2Scroll - 1) : 0;
    } else if (hit(tx, ty, TFT_WIDTH - HMI2_RAIL_W + 3,
                   TFT_HEIGHT - HMI2_FOOTER_H - 42, 42, 37)) {
      hmi2Scroll = (hmi2Scroll < maxFirst)
                        ? static_cast<uint8_t>(hmi2Scroll + 1)
                        : maxFirst;
    }
    if (previousScroll != hmi2Scroll) requestRedraw(DisplayRedrawReason::TAP_ACTION);
    return true;
  }
  if (ty >= HMI2_HEADER_H &&
      ty < HMI2_HEADER_H + HMI2_VISIBLE_ROWS * HMI2_ROW_H) {
    const uint16_t index = HmiPage::rowIndexAt(
        ty, HMI2_HEADER_H, HMI2_ROW_H, hmi2Scroll, total,
        HMI2_VISIBLE_ROWS);
    if (index < total) {
      const Hmi2Row &entry = g_hmi2Rows[index];
      if (hmi2Subpage == Hmi2Subpage::Modes) {
        if (index < sizeof(kHmi2Modes) / sizeof(kHmi2Modes[0])) beginModeSetup(kHmi2Modes[index]);
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::Profiles) {
        if (selectSetupProfile(index)) popScreen();
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::Wifi) {
        if (index == 1) {
          hmi2ScanWifiNetworks();
          hmi2Subpage = Hmi2Subpage::WifiNetworks;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
        } else if (index == 4) {
          hmi2OpenWifiPassword(hmi2CurrentSsid());
        }
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::WifiNetworks) {
        if (index == 0) {
          hmi2ScanWifiNetworks();
          requestFullScreenRedraw();
        } else if (index - 1 < hmi2WifiNetworkCount) {
          hmi2OpenWifiPassword(hmi2WifiNetworks[index - 1]);
        }
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::Sensors) {
        if (index < TEMP_COUNT) {
          hmi2SensorRole = index;
          hmi2SensorMessage[0] = '\0';
          hmi2ScanSensors();
          hmi2Subpage = Hmi2Subpage::SensorAddresses;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
        } else if (index == TEMP_COUNT) {
          if (!g_hmi2ScanCount) hmi2ScanSensors();
          hmi2AutoAssignSensors();
          requestFullScreenRedraw();
        }
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::SensorAddresses) {
        if (index == 0) {
          hmi2ClearSensorRole(hmi2SensorRole);
        } else if (index - 1 < g_hmi2ScanCount) {
          const uint8_t addressIndex = static_cast<uint8_t>(index - 1);
          if (hmi2SensorRoleOwner(g_hmi2ScanAddresses[addressIndex],
                                  hmi2SensorRole) >= 0) {
            snprintf(hmi2SensorMessage, sizeof(hmi2SensorMessage), "%s",
                     "Адрес уже занят другой ролью");
          } else if (hmi2AssignSensorAddress(hmi2SensorRole,
                                             g_hmi2ScanAddresses[addressIndex])) {
            hmi2Subpage = Hmi2Subpage::Sensors;
            hmi2Scroll = 0;
          }
        }
        requestFullScreenRedraw();
        return true;
      }
      if (hmi2Subpage == Hmi2Subpage::SensorOffsets) {
        if (index < TEMP_COUNT) {
          ui.sensorCalibrationTempRole = index;
          openValueEdit(EditableFields::Id::SENSOR_TEMPERATURE_OFFSET_C,
                        g_settings.tempCal.offsets[index],
                        saveSensorTemperatureOffset);
        }
        return true;
      }
      if (hmi2Subpage != Hmi2Subpage::None) return true;
      if (entry.type == Hmi2RowType::Read && (hmi2Section == 0 || hmi2Section == 7) &&
          (strcmp(entry.label, "Куб") == 0 || strcmp(entry.label, "Низ колонны") == 0)) {
        hmi2TempsReturnSection = hmi2Section;
       hmi2TempsReturnScroll = hmi2Scroll;
        hmi2TempsPage = true;
        requestFullScreenRedraw();
        armScreenSwitchGuard();
        return true;
      }
      if ((entry.type == Hmi2RowType::Bool ||
           entry.type == Hmi2RowType::Value) &&
          hmi2Section >= 2 && hmi2Section <= 5 &&
          (!isScreenEditable(UI_EQUIPMENT, state) ||
           hmi2FieldLocked(entry.field))) {
        return true;
      }
      if (entry.type == Hmi2RowType::Link) {
        if (strcmp(entry.label, "Wi-Fi") == 0) {
          hmi2Subpage = Hmi2Subpage::Wifi;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Режим") == 0) {
          pushScreen(UI_MODE_PICKER);
          return true;
        }
        if (strcmp(entry.label, "Профиль") == 0) {
          openSetupProfilePicker();
          return true;
        }
        if (strcmp(entry.label, "Параметры режима") == 0) {
          if (ui.setupDraft.valid) openModeParameters(ui.setupDraft.mode);
          else pushScreen(UI_MODE_PICKER);
          return true;
        }
        if (strcmp(entry.label, "Старт процесса") == 0) {
          if (!ui.setupDraft.valid) { pushScreen(UI_MODE_PICKER); return true; }
          if (requireModeSetupDraftResolution()) return true;
          refreshTftPreflight();
          pushScreen(UI_PREFLIGHT);
          return true;
        }
        if (strcmp(entry.label, "Параметры процесса") == 0) {
          pushScreen(UI_PROCESS_PARAMS);
          return true;
        }
        if (strcmp(entry.label, "Журнал") == 0) {
          openProcessEvents();
          return true;
        }
        if (strcmp(entry.label, "Все температуры") == 0) {
          hmi2TempsReturnSection = hmi2Section;
          hmi2TempsReturnScroll = hmi2Scroll;
    hmi2TempsPage = true;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Проверка оборудования") == 0 ||
            strcmp(entry.label, "Проверка оборуд.") == 0) {
          pushScreen(UI_SERVICE_TEST);
          return true;
        }
        if (strcmp(entry.label, "Дозирование") == 0) {
          pushScreen(UI_DOSING);
          return true;
        }
        if (strcmp(entry.label, "Калибровка тача") == 0) {
          hmi2TouchCalibrationPage = true;
          hmi2TouchCalibrationMessage[0] = '\0';
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Начать калибровку") == 0) {
          if (isModeRunning(state)) {
            snprintf(hmi2TouchCalibrationMessage,
                     sizeof(hmi2TouchCalibrationMessage), "%s",
                     "Сначала остановите процесс");
          } else if (!touch_ok) {
            snprintf(hmi2TouchCalibrationMessage,
                     sizeof(hmi2TouchCalibrationMessage), "%s",
                     "Сенсорный экран недоступен");
          } else {
            hmi2TouchCalibrationMessage[0] = '\0';
            Display::startTouchCalibration();
          }
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Сброс калибровки") == 0) {
          if (isModeRunning(state)) {
            snprintf(hmi2TouchCalibrationMessage,
                     sizeof(hmi2TouchCalibrationMessage), "%s",
                     "Сначала остановите процесс");
            requestFullScreenRedraw();
            return true;
          }
          const TouchCalibration previous = g_settings.touchCal;
          g_settings.touchCal.xMin = TOUCH_CAL_X_MIN;
          g_settings.touchCal.xMax = TOUCH_CAL_X_MAX;
          g_settings.touchCal.yMin = TOUCH_CAL_Y_MIN;
          g_settings.touchCal.yMax = TOUCH_CAL_Y_MAX;
          g_settings.touchCal.valid = false;
          if (!NVSManager::saveSettings(g_settings)) {
            g_settings.touchCal = previous;
            snprintf(hmi2TouchCalibrationMessage,
                     sizeof(hmi2TouchCalibrationMessage), "%s",
                     "Ошибка записи координат");
          } else {
            snprintf(hmi2TouchCalibrationMessage,
                     sizeof(hmi2TouchCalibrationMessage), "%s",
                     "Заводские координаты активны");
          }
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Назначение датчиков темп.") == 0) {
          if (!isScreenEditable(UI_SENSOR_CALIBRATION, state)) return true;
          hmi2SensorMessage[0] = '\0';
          hmi2ScanSensors();
          hmi2Subpage = Hmi2Subpage::Sensors;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Калибр. датчиков темп.") == 0) {
          if (!isScreenEditable(UI_SENSOR_CALIBRATION, state)) return true;
          hmi2SensorMessage[0] = '\0';
          hmi2Subpage = Hmi2Subpage::SensorOffsets;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Калибр. насоса") == 0) {
          if (!isScreenEditable(UI_PUMP_CALIBRATION, state)) {
            return true;
          }
          ui.pumpCalibrationMessage[0] = '\0';
          pushScreen(UI_PUMP_CALIBRATION);
          return true;
        }
        if (strcmp(entry.label, "Безопасность") == 0) {
          hmi2Section = 5;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
        if (strcmp(entry.label, "Сервис") == 0) {
          hmi2Section = 6;
          hmi2Scroll = 0;
          requestFullScreenRedraw();
          return true;
        }
      }
      if (entry.type == Hmi2RowType::Bool) {
        hmi2ToggleSetting(entry.field);
        requestRedraw(DisplayRedrawReason::TAP_ACTION);
        return true;
      }
      if (entry.type == Hmi2RowType::Value) {
        const uint8_t visibleRow = static_cast<uint8_t>(index - hmi2Scroll);
        const int16_t rowY = HMI2_HEADER_H + visibleRow * HMI2_ROW_H;
        const int16_t rightEdge =
            HMI2_MARGIN_X +
            (TFT_WIDTH - 2 * HMI2_MARGIN_X - HMI2_RAIL_W) - 10;
        const int16_t stepY = rowY + (HMI2_ROW_H - 40) / 2;
        const int16_t stepW = 44;
        if (hit(tx, ty, rightEdge - stepW, stepY, stepW, 40)) {
          hmi2AdjustValue(entry.field, 1, false);
          return true;
        }
        if (hit(tx, ty, rightEdge - 2 * stepW - 4, stepY, stepW, 40)) {
          hmi2AdjustValue(entry.field, -1, false);
          return true;
        }
        hmi2OpenValueEditor(entry.field);
        return true;
      }
    }
    return true;
  }
  if (ty >= TFT_HEIGHT - HMI2_FOOTER_H) {
    if (ui.currentScreen == UI_MODE_SETUP && !isModeRunning(state)) {
      if (hit(tx, ty, 322, 277, 152, 38) && hasUnsavedModeSetupDraft()) {
        char reason[144];
        if (!saveModeSetupDraft(reason, sizeof(reason))) setModeSetupDraftMessage(reason);
        requestFullScreenRedraw();
        return true;
      }
      if (hit(tx, ty, 6, 277, 152, 38)) {
        if (!requireModeSetupDraftResolution()) popScreen();
        return true;
      }
    }
    if (isModeRunning(g_state)) {
      if (hit(tx, ty, 6, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
        if (g_state.paused && ServiceTestSession::blocksProcessResume()) return true;
        enqueueTftProcessCommand(
            g_state.paused ? ControlCommandQueue::Type::Resume
                           : ControlCommandQueue::Type::Pause,
            ControlCommandQueue::Reason::ProcessAction);
        requestFullScreenRedraw();
        return true;
      }
      if (hit(tx, ty, 164, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
        openOperatorOverlay(TftOperatorOverlayLogic::Action::ProcessStop);
        requestFullScreenRedraw();
        return true;
      }
      if (hit(tx, ty, 322, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
        hmi2Section = 6;
        hmi2Scroll = 0;
        requestFullScreenRedraw();
        return true;
      }
      return true;
    }
    if (hmi2TouchCalibrationPage) {
      if (hit(tx, ty, 6, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
        hmi2TouchCalibrationPage = false;
        hmi2TouchCalibrationMessage[0] = '\0';
        hmi2Scroll = 0;
        requestFullScreenRedraw();
        return true;
      }
      if (hit(tx, ty, 164, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
        hmi2TouchCalibrationPage = false;
        hmi2TouchCalibrationMessage[0] = '\0';
        hmi2Section = 0;
        hmi2Scroll = 0;
        requestFullScreenRedraw();
        return true;
      }
      return true;
    }
    if (hit(tx, ty, 6, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
      hmi2Section = static_cast<uint8_t>(
          (hmi2Section + kHmi2SectionCount - 1) % kHmi2SectionCount);
      hmi2Scroll = 0;
      requestFullScreenRedraw();
      return true;
    }
    if (hit(tx, ty, 164, TFT_HEIGHT - HMI2_FOOTER_H + 5, 152, 38)) {
      hmi2Section = 0;
      hmi2Scroll = 0;
      requestFullScreenRedraw();
      return true;
    }
    return true;
  }
  return true;
}

