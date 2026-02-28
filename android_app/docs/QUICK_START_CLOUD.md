# Быстрый старт: Облачный прокси (без настройки роутера)

## Вариант 1: Готовый сервис (5 минут) ⭐

### Railway.app (Рекомендуется)

1. **Зарегистрируйтесь:** https://railway.app
2. **Создайте проект:** New Project → Deploy from GitHub
3. **Подключите репозиторий** с `cloud_proxy` папкой
4. **Установите переменные:**
   - `ESP32_TOKEN` = ваш секретный токен
   - `CLIENT_TOKEN` = ваш секретный токен для клиентов
5. **Получите URL:** `your-app.railway.app`

### Использование

**В Android приложении:**
- Хост: `your-app.railway.app`
- Порт: `443` (или оставить пустым)
- HTTPS: Включено

**В ESP32:**
- URL: `wss://your-app.railway.app/esp32?token=ESP32_TOKEN&device=DEVICE_ID`

---

## Вариант 2: Собственный VPS (15 минут)

### 1. Развертывание

```bash
# На VPS сервере
git clone <your-repo>
cd cloud_proxy
npm install
cp .env.example .env
# Отредактируйте .env
npm start
```

### 2. Настройка HTTPS (Nginx + Let's Encrypt)

```bash
sudo apt install nginx certbot
sudo certbot --nginx -d your-domain.com
```

### 3. Использование

**В Android приложении:**
- Хост: `your-domain.com`
- Порт: `443`
- HTTPS: Включено

---

## Вариант 3: Docker (10 минут)

```bash
cd cloud_proxy
# Создайте .env файл
docker-compose up -d
```

---

## Настройка ESP32

1. Добавьте код подключения к прокси (см. `cloud_proxy/ESP32_INTEGRATION.md`)
2. В настройках ESP32 укажите:
   - URL прокси-сервера
   - Device ID (уникальный для каждого устройства)
   - Токен авторизации

---

## Проверка работы

1. **Проверьте сервер:**
   ```bash
   curl https://your-server.com/health
   ```

2. **Проверьте подключение ESP32:**
   - В логах сервера должно появиться: `[ESP32] Device connected: DEVICE_ID`

3. **Проверьте Android приложение:**
   - Добавьте устройство с URL сервера
   - Нажмите "Проверить подключение"

---

## Готово! ✅

Теперь вы можете подключаться к ESP32 из любой точки мира без настройки роутера.

