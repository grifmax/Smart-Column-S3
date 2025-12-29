# Быстрый старт: Timeweb (10 минут)

## Подготовка

- ✅ SSH доступ включен в панели Timeweb
- ✅ Node.js включен в панели Timeweb
- ✅ Поддомен создан (например: `smartcolumn.yourdomain.com`)

---

## ШАГ 1: Подключение по SSH (1 минута)

```bash
ssh co111685@vh456.timeweb.ru
# Введите пароль от панели управления
```

---

## ШАГ 2: Создание директории (30 секунд)

```bash
cd ~
mkdir -p smart-column-proxy
cd smart-column-proxy
```

---

## ШАГ 3: Загрузка файлов (2 минуты)

### Вариант A: Через Git

```bash
git clone https://github.com/grifmax/Smart-Column-S3.git temp
mv temp/cloud_proxy/* .
rm -rf temp
```

### Вариант B: Через FTP

Загрузите файлы `server.js` и `package.json` через файловый менеджер Timeweb.

---

## ШАГ 4: Установка зависимостей (1 минута)

```bash
npm install
```

---

## ШАГ 5: Настройка .env (2 минуты)

```bash
nano .env
```

Вставьте:

```env
PORT=3000
ESP32_TOKEN=сгенерируйте_токен_32_символа
CLIENT_TOKEN=сгенерируйте_другой_токен_32_символа
```

**Генерация токенов:**
```bash
node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

Выполните дважды для двух токенов.

Сохраните: `Ctrl+O`, `Enter`, `Ctrl+X`

---

## ШАГ 6: Установка PM2 (1 минута)

```bash
npm install -g pm2
```

---

## ШАГ 7: Запуск (30 секунд)

```bash
pm2 start server.js --name smart-column-proxy
pm2 startup
pm2 save
```

---

## ШАГ 8: Настройка поддомена (2 минуты)

1. В панели Timeweb: **"Домены"** → **"Мои домены"**
2. Создайте поддомен: `smartcolumn`
3. Укажите директорию: `/smart-column-proxy`
4. Установите SSL сертификат (Let's Encrypt)

---

## ШАГ 9: Проверка (30 секунд)

Откройте в браузере:
```
https://smartcolumn.yourdomain.com/health
```

Должен вернуться JSON: `{"status":"ok",...}`

---

## Готово! ✅

**URL для использования:** `https://smartcolumn.yourdomain.com`

**В Android приложении:**
- Хост: `smartcolumn.yourdomain.com`
- Device ID: `esp32-001`
- Токен: ваш `CLIENT_TOKEN`

---

## Полезные команды

```bash
# Логи
pm2 logs smart-column-proxy

# Перезапуск
pm2 restart smart-column-proxy

# Статус
pm2 status
```

---

**Подробная инструкция:** [TIMEWEB_DEPLOYMENT.md](TIMEWEB_DEPLOYMENT.md)

