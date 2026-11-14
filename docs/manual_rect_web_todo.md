# Веб-интерфейс - Что нужно доделать

## ✅ Backend API готов:

1. **GET `/api/settings/manual-rect`** - получение настроек ✅
2. **POST `/api/settings/manual-rect`** - сохранение настроек ✅
3. **POST `/api/process/manual-rect/confirm-container`** - подтверждение смены тары ✅
4. **GET `/api/status`** - добавлены поля `manualRectContainerChangePending`, `manualRectPendingPhase` ✅

## ⏳ Что нужно добавить в веб-интерфейс:

### 1. Страница настроек ручной ректификации

**Файл:** `src/web/settings/manual-rect.js` (создать новый)

```javascript
// Загрузка настроек
async function loadManualRectSettings() {
    const response = await fetch('/api/settings/manual-rect');
    const settings = await response.json();
    
    document.getElementById('heads-power').value = settings.headsPowerW;
    document.getElementById('body-power').value = settings.bodyPowerW;
    document.getElementById('tails-power').value = settings.tailsPowerW;
    document.getElementById('heads-speed').value = settings.headsSpeedMlH;
    document.getElementById('body-speed').value = settings.bodySpeedMlH;
    document.getElementById('heads-volume').value = settings.headsTargetVolumeMl;
    document.getElementById('body-volume').value = settings.bodyTargetVolumeMl;
    document.getElementById('tails-volume').value = settings.tailsTargetVolumeMl;
    document.getElementById('sound-enabled').checked = settings.fractionChangeSoundEnabled;
}

// Сохранение настроек
async function saveManualRectSettings() {
    const settings = {
        headsPowerW: parseFloat(document.getElementById('heads-power').value),
        bodyPowerW: parseFloat(document.getElementById('body-power').value),
        tailsPowerW: parseFloat(document.getElementById('tails-power').value),
        headsSpeedMlH: parseFloat(document.getElementById('heads-speed').value),
        bodySpeedMlH: parseFloat(document.getElementById('body-speed').value),
        headsTargetVolumeMl: parseFloat(document.getElementById('heads-volume').value),
        bodyTargetVolumeMl: parseFloat(document.getElementById('body-volume').value),
        tailsTargetVolumeMl: parseFloat(document.getElementById('tails-volume').value),
        fractionChangeSoundEnabled: document.getElementById('sound-enabled').checked
    };

    const response = await fetch('/api/settings/manual-rect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(settings)
    });

    const result = await response.json();
    if (result.success) {
        alert('✅ Настройки сохранены');
    } else {
        alert('❌ Ошибка: ' + result.error);
    }
}
```

**HTML для настроек** (добавить в `data/index.html` в секцию настроек):

```html
<div class="settings-section" id="manual-rect-settings">
    <h3>Ручная ректификация</h3>
    
    <div class="settings-group">
        <h4>Мощность ТЭНа (Ватты)</h4>
        <label>
            Головы: <input type="number" id="heads-power" min="100" max="10000" step="100" />
        </label>
        <label>
            Тело: <input type="number" id="body-power" min="100" max="10000" step="100" />
        </label>
        <label>
            Хвосты: <input type="number" id="tails-power" min="100" max="10000" step="100" />
        </label>
    </div>

    <div class="settings-group">
        <h4>Скорость отбора (мл/ч)</h4>
        <label>
            Головы: <input type="number" id="heads-speed" min="10" max="5000" step="10" />
        </label>
        <label>
            Тело: <input type="number" id="body-speed" min="10" max="5000" step="10" />
        </label>
    </div>

    <div class="settings-group">
        <h4>Автоматическая смена фракций (0 = ручная смена)</h4>
        <label>
            Целевой объем голов (мл): <input type="number" id="heads-volume" min="0" max="50000" step="50" />
        </label>
        <label>
            Целевой объем тела (мл): <input type="number" id="body-volume" min="0" max="50000" step="50" />
        </label>
        <label>
            Целевой объем хвостов (мл): <input type="number" id="tails-volume" min="0" max="50000" step="50" />
        </label>
    </div>

    <div class="settings-group">
        <label>
            <input type="checkbox" id="sound-enabled" />
            Звуковой сигнал при смене фракций
        </label>
    </div>

    <button onclick="saveManualRectSettings()">💾 Сохранить настройки</button>
</div>
```

### 2. Модальное окно смены тары

**Файл:** `src/web/runtime/container-change-modal.js` (создать новый)

```javascript
export function showContainerChangeModal(pendingPhaseStr) {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML = `
        <div class="modal-dialog">
            <div class="modal-header">
                <h3>🚨 Смена тары</h3>
            </div>
            <div class="modal-body">
                <p><strong>Отбор остановлен!</strong></p>
                <p>Смените тару для следующей фракции: <strong>${pendingPhaseStr}</strong></p>
                <p>После смены тары нажмите "Подтвердить" для продолжения отбора.</p>
            </div>
            <div class="modal-footer">
                <button class="btn btn-primary" onclick="confirmContainerChange()">
                    ✅ Подтвердить смену тары
                </button>
            </div>
        </div>
    `;
    document.body.appendChild(modal);
    
    // Звуковой сигнал
    playContainerChangeSound();
}

async function confirmContainerChange() {
    try {
        const response = await fetch('/api/process/manual-rect/confirm-container', {
            method: 'POST'
        });
        const result = await response.json();
        
        if (result.success) {
            // Закрыть модальное окно
            document.querySelector('.modal-overlay')?.remove();
            alert('✅ Смена тары подтверждена. Отбор возобновлён.');
            // Обновить статус
            setTimeout(() => window.loadStatus?.(), 500);
        } else {
            alert('❌ Ошибка: ' + result.message);
        }
    } catch (e) {
        alert('❌ Ошибка: ' + e.message);
    }
}

function playContainerChangeSound() {
    const audio = new Audio('data:audio/wav;base64,UklGRnoGAABXQVZFZm10IBAAAAABAAEAQB8AAEAfAAABAAgAZGF0YQoGAACBhYqFbF1fdJivrJBhNjVgodDbq2EcBj+a2/LDciUFLIHO8tiJNwgZaLvt559NEAxQp+PwtmMcBjiR1/LMeSwFJHfH8N2QQAoUXrTp66hVFApGn+DyvmwhBTGH0fPTgjMGHm7A7+OZSA0PVKzn77BdGgs+ltzvxHIn');
    audio.play().catch(() => {}); // Игнорируем ошибки автовоспроизведения
}

// Глобальная функция для вызова из HTML
window.confirmContainerChange = confirmContainerChange;
```

### 3. Проверка состояния смены тары в status.js

**Файл:** `src/core/status.js` (добавить в существующий)

```javascript
// Добавить в функцию обновления статуса:
if (data.mode === 7 && data.manualRectContainerChangePending) {  // 7 = MANUAL_RECT
    import('./runtime/container-change-modal.js').then(module => {
        module.showContainerChangeModal(data.manualRectPendingPhaseStr || 'Следующая фракция');
    });
}
```

### 4. CSS для модального окна

**Файл:** `data/style.css` (добавить)

```css
.modal-overlay {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background: rgba(0, 0, 0, 0.7);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 10000;
}

.modal-dialog {
    background: var(--bg-content);
    border-radius: 8px;
    max-width: 500px;
    width: 90%;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    animation: modalFadeIn 0.2s ease-out;
}

@keyframes modalFadeIn {
    from { opacity: 0; transform: translateY(-20px); }
    to { opacity: 1; transform: translateY(0); }
}

.modal-header {
    padding: 20px;
    border-bottom: 1px solid var(--border-color);
}

.modal-header h3 {
    margin: 0;
    color: var(--danger, #dc3545);
}

.modal-body {
    padding: 20px;
}

.modal-footer {
    padding: 20px;
    border-top: 1px solid var(--border-color);
    text-align: right;
}

.btn-primary {
    background: var(--primary, #2196f3);
    color: white;
    border: none;
    padding: 10px 20px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 16px;
}

.btn-primary:hover {
    opacity: 0.9;
}
```

## 📝 Итоговый чеклист:

- [ ] Создать `src/web/settings/manual-rect.js`
- [ ] Добавить HTML форму в `data/index.html`
- [ ] Создать `src/web/runtime/container-change-modal.js`
- [ ] Добавить проверку в `src/core/status.js`
- [ ] Добавить CSS в `data/style.css`
- [ ] Подключить скрипты в HTML
- [ ] Протестировать на устройстве

## 🚀 Готовность:

**Backend:** 100% ✅  
**Frontend:** 0% ⏳ (нужно создать файлы выше)

После создания этих файлов веб-интерфейс будет полностью функционален!
