# Smart-Column S3 — Руководство по интеграции с Home Assistant

**Версия:** 1.3+
**Последнее обновление:** 2025-12-08

## Содержание

1. [Обзор](#обзор)
2. [Требования](#требования)
3. [Настройка MQTT брокера](#настройка-mqtt-брокера)
4. [Настройка устройства](#настройка-устройства)
5. [Настройка Home Assistant](#настройка-home-assistant)
6. [Push-уведомления](#push-уведомления)
7. [Примеры панелей управления](#примеры-панелей-управления)
8. [Автоматизации](#автоматизации)
9. [Energy Dashboard](#energy-dashboard)
10. [Решение проблем](#решение-проблем)

---

## Обзор

Smart-Column S3 легко интегрируется с Home Assistant через протокол MQTT Discovery. После настройки все датчики и сущности обнаруживаются автоматически и готовы к использованию.

### Возможности

- 🔍 **Автоматическое обнаружение** - Не требуется ручная настройка сущностей
- 📊 **Мониторинг в реальном времени** - Температура, мощность, энергопотребление
- ⚡ **Energy Dashboard** - Отслеживание потребления энергии
- 🔔 **Push-уведомления** - Автоматические уведомления о событиях и ошибках
- 🚨 **Автоматизации** - Оповещения и автоматические действия
- 📱 **Мобильный доступ** - Мониторинг из любого места через приложение HA

---

## Требования

### Обязательные

- Home Assistant (установленный и работающий)
- MQTT Брокер (рекомендуется Mosquitto)
- Устройство Smart-Column S3 в той же сети
- Сетевое подключение между HA и устройством

### Опциональные

- Node-RED для продвинутых автоматизаций

---

## Настройка MQTT брокера

### Вариант 1: Дополнение Mosquitto (Рекомендуется)

1. **Установка Mosquitto Broker**
   - Перейдите в: Настройки → Дополнения → Магазин дополнений
   - Найдите "Mosquitto broker"
   - Нажмите "Установить"

2. **Настройка Mosquitto**

   Конфигурация дополнения:
   ```yaml
   logins:
     - username: mqtt_user
       password: ваш_надежный_пароль
   require_certificate: false
   certfile: fullchain.pem
   keyfile: privkey.pem
   ```

3. **Запуск дополнения**
   - Включите "Запускать при загрузке"
   - Нажмите "ЗАПУСТИТЬ"

4. **Проверка установки**
   - Проверьте вкладку Журнал на успешный запуск

### Вариант 2: Внешний Mosquitto

Если используете внешний Mosquitto:

```bash
# Установка Mosquitto
sudo apt-get install mosquitto mosquitto-clients

# Создание файла паролей
sudo mosquitto_passwd -c /etc/mosquitto/passwd mqtt_user

# Редактирование конфигурации
sudo nano /etc/mosquitto/mosquitto.conf
```

Добавьте:
```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Перезапуск:
```bash
sudo systemctl restart mosquitto
```

---

## Настройка устройства

### Доступ к веб-интерфейсу

1. Откройте браузер: `http://<ip-устройства>`
2. Перейдите в Настройки → MQTT
3. Настройте следующим образом:

### Параметры MQTT

| Параметр | Значение | Пример |
|----------|----------|--------|
| **Включить MQTT** | ☑ Включено | |
| **MQTT сервер** | IP HA или hostname | `192.168.1.50` или `homeassistant.local` |
| **MQTT порт** | По умолчанию: 1883 | `1883` |
| **Имя пользователя** | Из конфигурации Mosquitto | `mqtt_user` |
| **Пароль** | Из конфигурации Mosquitto | `ваш_надежный_пароль` |
| **Базовый топик** | Оставьте по умолчанию | `smartcolumn` |
| **Включить Discovery** | ☑ Включено | |
| **Интервал публикации** | 10000 мс (10 сек) | `10000` |

4. Нажмите **Сохранить настройки**
5. Устройство перезагрузится и подключится к MQTT брокеру

### Проверка подключения

Проверьте журнал устройства:
```
[MQTT] Подключение к 192.168.1.50:1883...
[MQTT] Успешно подключено
[MQTT] Публикация сообщений обнаружения...
[MQTT] Обнаружение завершено - опубликовано 6 сущностей
```

---

## Настройка Home Assistant

### Добавление интеграции MQTT

1. **Перейдите в Интеграции**
   - Настройки → Устройства и службы → Интеграции

2. **Добавить MQTT**
   - Нажмите "+ ДОБАВИТЬ ИНТЕГРАЦИЮ"
   - Найдите "MQTT"
   - Выберите "MQTT"

3. **Настройка подключения**
   ```
   Брокер: localhost (если используете дополнение) или IP адрес
   Порт: 1883
   Имя пользователя: mqtt_user
   Пароль: ваш_надежный_пароль
   ```

4. **Включить обнаружение**
   - Префикс обнаружения: `homeassistant` (по умолчанию)
   - ☑ Включить новые добавленные сущности

5. **Отправить**

### Проверка автоматического обнаружения

После подключения устройства проверьте:

**Настройки → Устройства и службы → MQTT**

Вы должны увидеть новое устройство:
```
Smart Column abc123
  - Температура куба
  - Температура царги верх
  - Напряжение
  - Ток
  - Мощность
  - Энергия
  - Здоровье системы
  - Последнее уведомление
```

Если не видно, смотрите раздел [Решение проблем](#решение-проблем).

---

## Push-уведомления

**Версия:** 1.3+

Smart-Column S3 автоматически отправляет push-уведомления через MQTT о важных событиях процесса и критических ошибках.

### Типы уведомлений

#### 📋 События процесса (info/success)
- Запуск процесса ректификации
- Смена фаз (Разогрев → Стабилизация → Отбор голов → Отбор тела)
- Завершение процесса с итоговой статистикой

#### ⚠️ Предупреждения (warning)
- Остановка процесса пользователем
- Захлёб колонны (мощность автоматически снижена)
- Обычные ошибки системы

#### ❌ Критические ошибки (error)
- Прорыв паров (критическая температура TSA)
- Перегрев воды охлаждения
- Потеря связи с датчиками температуры
- Критические и фатальные ошибки модулей

### Автоматическая интеграция

Устройство публикует уведомления в два MQTT топика:

1. **`smartcolumn/{deviceId}/notification`** - Sensor для Home Assistant
   - Отображает последнее уведомление как sensor
   - Атрибуты: title, message, level, timestamp

2. **`smartcolumn/{deviceId}/notify`** - Для notify service
   - Формат готов для использования с MQTT notify platform
   - Автоматическое добавление эмодзи: ℹ️ info, ⚠️ warning, ❌ error, ✅ success

### Настройка MQTT Notify (опционально)

Для более гибких уведомлений настройте MQTT notify в `configuration.yaml`:

```yaml
notify:
  - name: smart_column_notify
    platform: mqtt
    command_topic: "smartcolumn/ABC123/notify"
```

Замените `ABC123` на ID вашего устройства.

### Автоматизации для уведомлений

#### Уведомление о смене фазы процесса

```yaml
alias: Smart Column - Уведомление о фазах
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_notification
condition:
  - condition: template
    value_template: "{{ trigger.to_state.attributes.level in ['info', 'success'] }}"
action:
  - service: notify.mobile_app
    data:
      title: "{{ trigger.to_state.attributes.title }}"
      message: "{{ trigger.to_state.attributes.message }}"
```

#### Критические уведомления с высоким приоритетом

```yaml
alias: Smart Column - Критические уведомления
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_notification
condition:
  - condition: template
    value_template: "{{ trigger.to_state.attributes.level == 'error' }}"
action:
  - service: notify.mobile_app
    data:
      title: "рџљЁ {{ trigger.to_state.attributes.title }}"
      message: "{{ trigger.to_state.attributes.message }}"
      data:
        priority: high
        ttl: 0
        notification_icon: mdi:alert-circle
        color: red
        tag: smart_column_critical
        actions:
          - action: VIEW_DEVICE
            title: Открыть устройство
```

```yaml
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_notification
action:
    data:
      message: |
        {{ trigger.to_state.attributes.title }}

        {{ trigger.to_state.attributes.message }}

        рџ•ђ {{ now().strftime('%H:%M:%S') }}
```

#### Уведомление о завершении процесса

```yaml
alias: Smart Column - Процесс завершён
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_notification
condition:
  - condition: template
    value_template: "{{ 'завершён' in trigger.to_state.attributes.message | lower }}"
action:
  - service: notify.mobile_app
    data:
      title: "✅ Процесс завершён"
      message: "{{ trigger.to_state.attributes.message }}"
      data:
        notification_icon: mdi:check-circle
        color: green
```

### Примеры уведомлений

**Запуск процесса:**
```
ℹ️ Процесс запущен
Начат процесс ректификации - фаза разогрева
```

**Смена фазы:**
```
ℹ️ Фаза: Отбор тела
Продувка завершена, начат отбор тела (основной продукт)
```

**Завершение:**
```
✅ Процесс завершён
Процесс завершён! Собрано: 2850 мл
```

**Критическая ошибка:**
```
❌ КРИТИЧЕСКАЯ ОШИБКА
Прорыв паров! Температура TSA: 62.3°C
```

### Карточка последнего уведомления

Добавьте на панель управления для отображения последнего уведомления:

```yaml
type: entities
title: 🔔 Последнее уведомление
entities:
  - entity: sensor.smart_column_abc123_notification
    name: Событие
    icon: mdi:bell-alert
  - type: attribute
    entity: sensor.smart_column_abc123_notification
    attribute: message
    name: Сообщение
  - type: attribute
    entity: sensor.smart_column_abc123_notification
    attribute: level
    name: Уровень
  - type: attribute
    entity: sensor.smart_column_abc123_notification
    attribute: timestamp
    name: Время
```

---

## Примеры панелей управления

### Простая карточка состояния

```yaml
type: entities
title: Статус Smart Column
entities:
  - entity: sensor.smart_column_abc123_cube_temp
    name: Температура куба
  - entity: sensor.smart_column_abc123_column_top
    name: Температура царги
  - entity: sensor.smart_column_abc123_voltage
    name: Напряжение
  - entity: sensor.smart_column_abc123_power
    name: Мощность
  - entity: sensor.smart_column_abc123_health
    name: Здоровье системы
```

### Датчик температуры

```yaml
type: gauge
entity: sensor.smart_column_abc123_cube_temp
name: Температура куба
min: 0
max: 100
severity:
  green: 0
  yellow: 70
  red: 90
needle: true
```

### Мониторинг мощности

```yaml
type: vertical-stack
cards:
  - type: gauge
    entity: sensor.smart_column_abc123_power
    name: Текущая мощность
    min: 0
    max: 5000
    unit: Вт
    severity:
      green: 0
      yellow: 2500
      red: 4000

  - type: sensor
    entity: sensor.smart_column_abc123_energy
    name: Всего энергии
    graph: line
    detail: 2
```

### График истории

```yaml
type: history-graph
title: История температур
hours_to_show: 24
entities:
  - entity: sensor.smart_column_abc123_cube_temp
    name: Куб
  - entity: sensor.smart_column_abc123_column_top
    name: Царга верх
```

### Полная панель управления

```yaml
type: vertical-stack
title: 🥃 Мониторинг Smart Column
cards:
  # Обзор состояния
  - type: horizontal-stack
    cards:
      - type: gauge
        entity: sensor.smart_column_abc123_cube_temp
        name: Куб
        min: 0
        max: 100
        severity:
          green: 0
          yellow: 70
          red: 90

      - type: gauge
        entity: sensor.smart_column_abc123_health
        name: Р—доровье
        min: 0
        max: 100
        severity:
          red: 0
          yellow: 50
          green: 80

  # Мониторинг мощности
  - type: horizontal-stack
    cards:
      - type: entity
        entity: sensor.smart_column_abc123_voltage
        name: Напряжение
        icon: mdi:flash

      - type: entity
        entity: sensor.smart_column_abc123_current
        name: Ток
        icon: mdi:current-ac

      - type: entity
        entity: sensor.smart_column_abc123_power
        name: Мощность
        icon: mdi:lightning-bolt

  # История температур
  - type: history-graph
    title: Тренды температур
    hours_to_show: 12
    entities:
      - sensor.smart_column_abc123_cube_temp
      - sensor.smart_column_abc123_column_top

  # Потребление энергии
  - type: energy-date-selection
  - type: energy-sources-table
```

---

## Автоматизации

### Оповещение о высокой температуре

```yaml
alias: Smart Column - Оповещение о высокой температуре
trigger:
  - platform: numeric_state
    entity_id: sensor.smart_column_abc123_cube_temp
    above: 95
condition: []
action:
  - service: notify.mobile_app
    data:
      title: ⚠️ Оповещение Smart Column
      message: "Критическая температура куба: {{ states('sensor.smart_column_abc123_cube_temp') }}°C"
      data:
        priority: high
        ttl: 0
mode: single
```

### Предупреждение о низком здоровье системы

```yaml
alias: Smart Column - Предупреждение о низком здоровье
trigger:
  - platform: numeric_state
    entity_id: sensor.smart_column_abc123_health
    below: 70
condition: []
action:
  - service: notify.persistent_notification
    data:
      title: Низкое здоровье Smart Column
      message: "Здоровье системы {{ states('sensor.smart_column_abc123_health') }}%. Пожалуйста, проверьте устройство."
  - service: persistent_notification.create
    data:
      title: ⚠️ Smart Column
      message: "Р—доровье: {{ states('sensor.smart_column_abc123_health') }}%"
mode: single
```

```yaml
alias: Smart Column - Процесс завершён
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_cube_temp
    to: "25.0"
    for:
      minutes: 10
condition:
  - condition: template
    value_template: "{{ states('sensor.smart_column_abc123_cube_temp') | float < 30 }}"
action:
    data:
      message: |
        ✅ Процесс ректификации завершён!

        Итоговая статистика:
        🌡️ Куб: {{ states('sensor.smart_column_abc123_cube_temp') }}°C
        ⚡ Использовано энергии: {{ states('sensor.smart_column_abc123_energy') }} кВт⋅ч

        Устройство охлаждается.
mode: single
```

### Защита от превышения мощности

```yaml
alias: Smart Column - Превышение мощности
trigger:
  - platform: numeric_state
    entity_id: sensor.smart_column_abc123_power
    above: 4500
condition: []
action:
  - service: notify.mobile_app
    data:
      title: ⚠️ Превышение лимита мощности
      message: "Текущая мощность: {{ states('sensor.smart_column_abc123_power') }}Вт"
      data:
        priority: high
        actions:
          - action: STOP_COLUMN
            title: Остановить устройство
mode: single
```

### Ежедневный отчёт об энергии

```yaml
alias: Smart Column - Ежедневный отчёт
trigger:
  - platform: time
    at: "23:00:00"
condition:
  - condition: template
    value_template: "{{ states('sensor.smart_column_abc123_energy') | float > 0 }}"
action:
mode: single
```

---

## Energy Dashboard

### Настройка

1. **Перейдите в Energy Dashboard**
   - Настройки → Панели управления → Энергия

2. **Добавить электрическую сеть**
   - Нажмите "Добавить потребление"
   - Выберите "Отдельные устройства"

3. **Выбрать датчик энергии Smart Column**
   - Выберите: `sensor.smart_column_abc123_energy`
   - Единица: кВт⋅ч
   - Нажмите "Сохранить"

4. **Настроить стоимость (Опционально)**
   - Установите тариф на электроэнергию
   - Пример: 5.50 ₽/кВт⋅ч

### Отслеживание энергии

Устройство сообщает накопительное энергопотребление. Датчик использует `state_class: total_increasing`, что идеально подходит для Energy Dashboard.

**Возможности:**
- Графики потребления за день/месяц/год
- Расчёт стоимости
- Сравнение с другими устройствами
- Экспорт данных в CSV

### Продвинуто: Отслеживание энергии по процессам

Создайте utility meters для отслеживания по конкретным процессам:

```yaml
utility_meter:
  smart_column_process_energy:
    source: sensor.smart_column_abc123_energy
    cycle: none  # Ручной сброс

  smart_column_daily:
    source: sensor.smart_column_abc123_energy
    cycle: daily
```

Автоматизация для сброса счётчика процесса:

```yaml
alias: Smart Column - Сброс счётчика энергии процесса
trigger:
  - platform: state
    entity_id: sensor.smart_column_abc123_power
    from: "0"
    to: "0"
    for:
      hours: 1
action:
  - service: utility_meter.reset
    target:
      entity_id: utility_meter.smart_column_process_energy
```

---

## Решение проблем

### Устройство не обнаружено

**Проверьте MQTT подключение:**
1. Веб-интерфейс устройства → Статус
2. Ищите "MQTT: Подключено"

**Проверьте журналы Home Assistant MQTT:**
```
Настройки → Система → Журналы
Фильтр: mqtt
```

**Вручную прослушайте топики:**
```bash
mosquitto_sub -h localhost -u mqtt_user -P пароль -t 'smartcolumn/#' -v
```

Ожидаемый вывод:
```
smartcolumn/abc123/state {"mode":0,"temperatures":{...}}
smartcolumn/abc123/health 95
smartcolumn/abc123/status online
```

### Сущности показывают "Недоступно"

**Возможные причины:**

1. **Устройство офлайн**
   - Проверьте сетевое подключение устройства
   - Пингуйте IP устройства

2. **MQTT брокер не работает**
   - Настройки → Дополнения → Mosquitto broker
   - Проверьте статус и журналы

3. **Неверные учётные данные**
   - Проверьте совпадение username/password
   - Проверьте журналы Mosquitto на ошибки аутентификации

### Сообщения обнаружения не получены

**Принудительная повторная публикация:**

1. Веб-интерфейс устройства → Настройки → MQTT
2. Снимите галочку "Включить Discovery"
3. Сохранить
4. Поставьте галочку "Включить Discovery"
5. Сохранить

Устройство повторно опубликует все сообщения обнаружения.

**Ручная проверка обнаружения:**

```bash
mosquitto_sub -h localhost -u mqtt_user -P пароль -t 'homeassistant/#' -v
```

Должны появиться сообщения вида:
```
homeassistant/sensor/abc123_cube_temp/config {"name":"Температура куба",...}
```

### Значения энергии не обновляются

**Проверьте датчик энергии:**
- Инструменты разработчика → Состояния
- Найдите: `sensor.smart_column_abc123_energy`
- Проверьте `state_class: total_increasing`

**Проверьте настройки Energy Dashboard:**
- Энергия → Настройки
- Убедитесь, что датчик добавлен
- Проверьте единицу измерения: кВт⋅ч

**Принудительное обновление:**
- Веб-интерфейс устройства → Перезагрузка
- Дождитесь переподключения
- Проверьте Energy Dashboard через 1 минуту

### Высокий трафик MQTT

**Уменьшите интервал публикации:**

Веб-интерфейс устройства → Настройки → MQTT:
- Увеличьте "Интервал публикации"
- Рекомендуется: 10000-30000 мс
- Сохранить

**Настройки QoS:**

Для данных с низким приоритетом используйте QoS 0 вместо QoS 1.

---

## Лучшие практики

### Производительность

- Используйте интервал публикации ≥ 10 секунд
- Включайте обнаружение только когда необходимо
- Используйте QoS 0 для высокочастотных данных

### Безопасность

- Используйте надёжные пароли MQTT
- Включите аутентификацию на брокере
- Используйте TLS для внешнего доступа
- Держите прошивку обновлённой

### Надёжность

- Включите "Запускать при загрузке" для Mosquitto
- Используйте постоянные MQTT сессии
- Настройте LWT для отслеживания доступности
- Регулярные перезагрузки устройства (ежемесячно)

### Мониторинг

- Создайте автоматизацию проверки здоровья
- Отслеживайте тренды энергопотребления
- Настройте оповещения для критических значений
- Логируйте важные события

---

## Дополнительные ресурсы

### Официальная документация
- [Home Assistant MQTT](https://www.home-assistant.io/integrations/mqtt/)
- [MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/)
- [Energy Dashboard](https://www.home-assistant.io/docs/energy/)

### Сообщество
- [Форумы Home Assistant](https://community.home-assistant.io/)
- [r/homeassistant](https://reddit.com/r/homeassistant)

### Инструменты
- [MQTT Explorer](http://mqtt-explorer.com/) - Desktop MQTT клиент
- [Node-RED](https://nodered.org/) - Визуальные потоки автоматизации

---

## Пример: Полная настройка через configuration.yaml

Для продвинутых пользователей, вот полная настройка через `configuration.yaml`:

```yaml
# Конфигурация MQTT
mqtt:
  broker: localhost
  port: 1883
  username: !secret mqtt_user
  password: !secret mqtt_password
  discovery: true
  discovery_prefix: homeassistant
  birth_message:
    topic: 'hass/status'
    payload: 'online'
  will_message:
    topic: 'hass/status'
    payload: 'offline'

# Utility Meters для отслеживания энергии
utility_meter:
  smart_column_daily_energy:
    source: sensor.smart_column_abc123_energy
    cycle: daily

  smart_column_monthly_energy:
    source: sensor.smart_column_abc123_energy
    cycle: monthly

# Шаблонные датчики
template:
  - sensor:
      - name: "Статус Smart Column"
        state: >
          {% if states('sensor.smart_column_abc123_power') | float > 100 %}
            Работает
          {% else %}
            Простой
          {% endif %}
        icon: >
          {% if states('sensor.smart_column_abc123_power') | float > 100 %}
            mdi:flask
          {% else %}
            mdi:flask-empty-outline
          {% endif %}

      - name: "Эффективность Smart Column"
        unit_of_measurement: "%"
        state: >
          {{ states('sensor.smart_column_abc123_health') }}

# Автоматизации
automation:
  - alias: Smart Column Оповещение - Высокая температура
    trigger:
      - platform: numeric_state
        entity_id: sensor.smart_column_abc123_cube_temp
        above: 95
    action:
      - service: notify.mobile_app
        data:
          title: "⚠️ Высокая температура"
          message: "{{ states('sensor.smart_column_abc123_cube_temp') }}°C"
```

---

*Последнее обновление: 2025-12-06*
*Для поддержки: Проверьте журналы устройства или создайте issue на GitHub*
