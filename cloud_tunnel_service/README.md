# cloud_tunnel_service

Сервис туннеля для Smart-Column S3 (IoT модель):

- ESP32 держит исходящий `WSS` к этому сервису
- `cloud_proxy` (PHP) вызывает HTTP API этого сервиса, чтобы выполнить запрос на устройство через туннель

## Запуск (локально)

1) Скопировать `env.example` в переменные окружения (или экспортировать вручную)\n
2) Установить зависимости:

```bash
npm install
```

3) Dev:

```bash
npm run dev
```

4) Build+run:

```bash
npm run build
npm start
```

## Эндпоинты

- `GET /health`
- `POST /api/tunnel/request` (нужен заголовок `x-service-key`)
- `POST /api/tunnel/claim/commit` (нужен заголовок `x-service-key`)
- `GET /api/tunnel/devices/online?userId=...` (нужен заголовок `x-service-key`)

## WebSocket

По умолчанию: `WS_PATH=/ws/device`

ESP32 первым сообщением шлёт `hello`.

