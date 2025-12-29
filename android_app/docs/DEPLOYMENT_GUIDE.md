# Руководство по развертыванию облачного прокси

## Вариант 1: Быстрое развертывание (5 минут)

### Использование готового сервиса

1. **Railway.app** (бесплатный tier):
   - Зарегистрируйтесь на https://railway.app
   - Создайте новый проект
   - Подключите репозиторий с `cloud_proxy`
   - Установите переменные окружения
   - Получите URL: `your-app.railway.app`

2. **Render.com** (бесплатный tier):
   - Зарегистрируйтесь на https://render.com
   - Создайте Web Service
   - Подключите репозиторий
   - Установите переменные окружения
   - Получите URL: `your-app.onrender.com`

3. **Fly.io** (бесплатный tier):
   ```bash
   fly launch
   fly secrets set ESP32_TOKEN=your_token
   fly secrets set CLIENT_TOKEN=your_token
   ```

### Использование в приложении

После развертывания получите URL и используйте в настройках устройства:
- Хост: `your-app.railway.app` (или другой URL)
- Порт: `443` (или оставить пустым)
- HTTPS: Включено

---

## Вариант 2: Собственный VPS

### Требования

- VPS сервер (DigitalOcean, Hetzner, AWS - от $5/месяц)
- Ubuntu 20.04+ или Debian 11+
- Домен (опционально)

### Пошаговая инструкция

#### 1. Подготовка сервера

```bash
# Обновление системы
sudo apt update && sudo apt upgrade -y

# Установка Node.js
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs

# Установка PM2
sudo npm install -g pm2
```

#### 2. Развертывание приложения

```bash
# Клонирование репозитория
git clone <your-repo> /opt/smart-column-proxy
cd /opt/smart-column-proxy/cloud_proxy

# Установка зависимостей
npm install --production

# Создание .env файла
nano .env
# Вставьте:
# PORT=3000
# ESP32_TOKEN=your_secret_token_here
# CLIENT_TOKEN=your_client_token_here

# Запуск через PM2
pm2 start server.js --name smart-column-proxy
pm2 save
pm2 startup
```

#### 3. Настройка HTTPS (Let's Encrypt)

```bash
# Установка Certbot
sudo apt install certbot

# Получение сертификата
sudo certbot certonly --standalone -d your-domain.com

# Настройка Nginx
sudo apt install nginx
```

Создайте `/etc/nginx/sites-available/smart-column-proxy`:

```nginx
server {
    listen 80;
    server_name your-domain.com;
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name your-domain.com;

    ssl_certificate /etc/letsencrypt/live/your-domain.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/your-domain.com/privkey.pem;

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

```bash
sudo ln -s /etc/nginx/sites-available/smart-column-proxy /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

#### 4. Автообновление сертификата

```bash
sudo certbot renew --dry-run
# Добавьте в crontab:
# 0 0 * * * certbot renew --quiet
```

---

## Вариант 3: Docker развертывание

### Быстрый старт

```bash
cd cloud_proxy
docker-compose up -d
```

### Настройка переменных окружения

Создайте `.env` файл:

```env
ESP32_TOKEN=your_esp32_token
CLIENT_TOKEN=your_client_token
```

### Обновление

```bash
docker-compose pull
docker-compose up -d
```

---

## Проверка работы

### 1. Health check

```bash
curl https://your-domain.com/health
```

Должен вернуть:
```json
{
  "status": "ok",
  "timestamp": 1234567890,
  "devicesCount": 0
}
```

### 2. Список устройств

```bash
curl https://your-domain.com/api/devices
```

### 3. Тест WebSocket

Используйте онлайн инструмент: https://www.websocket.org/echo.html

Подключитесь к:
```
wss://your-domain.com/client?token=CLIENT_TOKEN&device=TEST_DEVICE
```

---

## Мониторинг и логи

### PM2

```bash
# Просмотр логов
pm2 logs smart-column-proxy

# Статус
pm2 status

# Перезапуск
pm2 restart smart-column-proxy
```

### Docker

```bash
# Логи
docker-compose logs -f

# Статус
docker-compose ps
```

---

## Безопасность

1. ✅ Используйте сильные токены (минимум 32 символа)
2. ✅ Настройте HTTPS (Let's Encrypt)
3. ✅ Ограничьте доступ по IP (опционально в Nginx)
4. ✅ Регулярно обновляйте зависимости
5. ✅ Используйте firewall (ufw)

```bash
# Настройка firewall
sudo ufw allow 22/tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw enable
```

---

## Рекомендуемые провайдеры VPS

1. **Hetzner** - от €4/месяц, хорошее соотношение цена/качество
2. **DigitalOcean** - от $6/месяц, удобная панель
3. **Vultr** - от $6/месяц, много локаций
4. **AWS Lightsail** - от $5/месяц, интеграция с AWS
5. **Linode** - от $5/месяц, надежный

---

## Стоимость

- **VPS:** от $5/месяц
- **Домен:** от $10/год (опционально, можно использовать IP)
- **SSL сертификат:** бесплатно (Let's Encrypt)
- **Итого:** от $5/месяц

---

## Поддержка

При возникновении проблем проверьте:
1. Логи сервера (`pm2 logs` или `docker-compose logs`)
2. Доступность портов (`netstat -tulpn`)
3. Конфигурацию Nginx (`sudo nginx -t`)
4. Firewall (`sudo ufw status`)

