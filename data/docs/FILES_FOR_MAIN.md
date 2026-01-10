# Файлы для smartcolumn/public_html/ (основной домен)

## 📋 Варианты организации

### Вариант 1: Редирект на поддомен (рекомендуется)

Если основной домен должен просто редиректить на поддомен прокси:

**Файл: `smartcolumn/public_html/index.php`**
```php
<?php
header('Location: https://smart-column-proxy.spiritcontrol.ru/');
exit;
```

Или через `.htaccess`:
```apache
Redirect permanent / https://smart-column-proxy.spiritcontrol.ru/
```

### Вариант 2: Основной сайт без изменений

Если основной домен `smartcolumn` не должен изменяться, ничего загружать не нужно.

### Вариант 3: Информационная страница с ссылкой

Если нужна информационная страница с ссылкой на прокси:

**Файл: `smartcolumn/public_html/index.html`**
```html
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart-Column S3</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            padding: 50px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: rgba(255,255,255,0.1);
            padding: 40px;
            border-radius: 12px;
            backdrop-filter: blur(10px);
        }
        h1 { margin-bottom: 20px; }
        a {
            display: inline-block;
            margin-top: 20px;
            padding: 15px 30px;
            background: white;
            color: #667eea;
            text-decoration: none;
            border-radius: 8px;
            font-weight: bold;
        }
        a:hover { background: #f0f0f0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🏭 Smart-Column S3</h1>
        <p>Веб-интерфейс управления колонной</p>
        <a href="https://smart-column-proxy.spiritcontrol.ru/">Перейти к управлению</a>
    </div>
</body>
</html>
```

## ✅ Рекомендация

Рекомендуется **Вариант 1** - простой редирект через `.htaccess` или `index.php`.

