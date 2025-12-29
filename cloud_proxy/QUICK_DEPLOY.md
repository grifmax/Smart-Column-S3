# Быстрое развертывание (5 минут)

## Минимальная инструкция

### 1. Регистрация (1 минута)
- Откройте https://railway.app
- Нажмите "Login with GitHub"
- Авторизуйтесь

### 2. Создание проекта (1 минута)
- Нажмите "New Project"
- Выберите "Deploy from GitHub repo"
- Выберите репозиторий Smart-Column-S3
- **Root Directory:** `cloud_proxy`
- Нажмите "Deploy"

### 3. Переменные (2 минуты)
- Settings → Variables
- Добавьте:
  - `ESP32_TOKEN` = сгенерируйте токен (32 символа)
  - `CLIENT_TOKEN` = сгенерируйте другой токен (32 символа)

**Генерация токенов:**
- Онлайн: https://www.random.org/strings/ (Length: 32, Count: 2)
- Или: `openssl rand -hex 32` (дважды)

### 4. Получение URL (30 секунд)
- Settings → Networking → Generate Domain
- Скопируйте URL: `your-project.railway.app`

### 5. Использование (30 секунд)
- В Android приложении:
  - Включите "Облачный прокси"
  - Хост: `your-project.railway.app`
  - Device ID: `esp32-001`
  - Токен: ваш `CLIENT_TOKEN`

## Готово! ✅

Теперь можно подключаться из любой точки мира.

