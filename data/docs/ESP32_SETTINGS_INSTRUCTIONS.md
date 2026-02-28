# Инструкция по добавлению настроек ESP32 в веб-интерфейс

## Шаги установки

### 1. Добавить HTML в index.html

Откройте файл `data/index.html` и найдите карточку WiFi (примерно строка 268-278).

После закрывающего тега `</div>` карточки WiFi (после строки 278), вставьте код из файла `ESP32_SETTINGS_HTML.txt`.

### 2. Добавить JavaScript функции в app.js

Откройте файл `data/app.js` и добавьте в конец файла код из `web_esp32_settings.js`.

Также добавьте в функцию `DOMContentLoaded` (примерно строка 21-34) вызов:
```javascript
loadESP32Settings();
```

И добавьте функцию для показа/скрытия полей:
```javascript
function toggleESP32Fields() {
    const enabled = document.getElementById('esp32-enabled').checked;
    const fields = document.getElementById('esp32-fields');
    if (fields) {
        fields.style.display = enabled ? 'block' : 'none';
    }
}
```

### 3. Скопировать файлы в web/

После модификации файлов, скопируйте их в `cloud_proxy/web/`:
```bash
cp data/index.html cloud_proxy/web/
cp data/app.js cloud_proxy/web/
```

### 4. Готово!

После этого настройки ESP32 появятся во вкладке "Настройки" веб-интерфейса.

