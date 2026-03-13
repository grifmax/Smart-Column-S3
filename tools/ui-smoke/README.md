# UI Smoke Tests

Smoke-тесты для Web UI поверх статической сборки из `data/`.

## Что проверяется

- вкладка управления режимами: состояние кнопок, `pause/resume`, защита от повторного старта;
- страница логов: загрузка системных событий, живой статус подключения, очистка журнала;
- страница графиков: старт WebSocket, инициализация ApexCharts и приём live-данных;
- мобильный accordion на вкладке `Инструменты`: возможность свернуть все калькуляторы и раскрытие вниз от текущего заголовка.

## Запуск

Из корня репозитория:

```bash
npm --prefix tools/ui-smoke install
npx --prefix tools/ui-smoke playwright install chromium
npm run test:ui-smoke
```

Локально из папки smoke-тестов:

```bash
cd tools/ui-smoke
npm install
npx playwright install chromium
npm test
```

Тесты поднимают локальный HTTP-сервер для `../../data` на `http://127.0.0.1:4173`, мокаются `API`-ответы, WebSocket и библиотека ApexCharts, поэтому прогоны не зависят от реального контроллера и CDN.
