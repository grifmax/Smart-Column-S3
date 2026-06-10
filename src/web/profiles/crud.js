import { currentProfileId, setCurrentProfileId } from './state.js';
import { loadProfilesList } from './list.js';

// Показать модальное окно создания профиля

export function showCreateProfileModal() {

    setCurrentProfileId(null);

    document.getElementById('profile-modal-title').textContent = 'Создание профиля';

    document.getElementById('profile-name').value = '';

    document.getElementById('profile-description').value = '';

    document.getElementById('profile-category').value = 'rectification';

    document.getElementById('profile-tags').value = '';

    document.getElementById('profile-modal').style.display = 'flex';

}



// Закрыть модальное окно создания

export function closeProfileModal() {

    document.getElementById('profile-modal').style.display = 'none';

}



// Сохранить профиль

export function saveProfile() {

    const name = document.getElementById('profile-name').value.trim();

    const description = document.getElementById('profile-description').value.trim();

    const category = document.getElementById('profile-category').value;

    const tagsStr = document.getElementById('profile-tags').value.trim();

    const tags = tagsStr ? tagsStr.split(',').map(t => t.trim()).filter(t => t) : [];



    if (!name) {

        alert('Пожалуйста, введите название профиля');

        return;

    }



    // TODO: Получить текущие параметры из формы управления

    // Пока используем значения по умолчанию

    const profile = {

        metadata: {

            name: name,

            description: description,

            category: category,

            tags: tags,

            author: 'user'

        },

        parameters: {

            mode: category,

            model: 'classic',

            heater: {

                maxPower: 3000,

                autoMode: true,

                pidKp: 2.0,

                pidKi: 0.5,

                pidKd: 1.0

            },

            rectification: {

                stabilizationMin: 20,

                headsVolume: 50,

                bodyVolume: 2000,

                tailsVolume: 100,

                headsSpeed: 150,

                bodySpeed: 300,

                tailsSpeed: 400,

                purgeMin: 5

            },

            distillation: {

                headsVolume: 0,

                targetVolume: 3000,

                speed: 500,

                endTemp: 96.0

            },

            temperatures: {

                maxCube: 98.0,

                maxColumn: 82.0,

                headsEnd: 78.5,

                bodyStart: 78.0,

                bodyEnd: 85.0

            },

            safety: {

                maxRuntime: 720,

                waterFlowMin: 2.0,

                pressureMax: 150

            }

        }

    };



    fetch('/api/profiles', {

        method: 'POST',

        headers: { 'Content-Type': 'application/json' },

        body: JSON.stringify(profile)

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                closeProfileModal();

                loadProfilesList();

                alert('✅ Профиль успешно создан!');

            } else {

                alert('❌ Ошибка создания профиля: ' + (data.error || 'Неизвестная ошибка'));

            }

        })

        .catch(error => {

            console.error('Ошибка сохранения профиля:', error);

            alert('❌ Ошибка сохранения профиля');

        });

}



// Просмотр профиля

export function viewProfile(id) {

    fetch(`/api/profiles/${id}`)

        .then(response => response.json())

        .then(profile => {

            showProfileViewModal(profile);

        })

        .catch(error => {

            console.error('Ошибка загрузки профиля:', error);

            alert('❌ Ошибка загрузки профиля');

        });

}

function formatProfileMinutes(seconds) {

    const value = Number(seconds || 0);
    return value > 0 ? `${Math.round(value / 60)} мин` : '—';

}

function formatProfileNumber(value, digits = 1, suffix = '') {

    const numeric = Number(value || 0);
    return numeric > 0 ? `${numeric.toFixed(digits)}${suffix}` : '—';

}



// Показать модальное окно просмотра профиля

export function showProfileViewModal(profile) {

    setCurrentProfileId(profile.id);

    document.getElementById('profile-view-title').textContent = profile.metadata.name;



    const body = document.getElementById('profile-view-body');

    const catNames = {

        'rectification': 'Ректификация',

        'distillation': 'Дистилляция',

        'mashing': 'Затирка'

    };



    const learning = profile.learning || {};
    const lastSuccessfulRun = learning.lastSuccessfulRun || null;
    const advisorItems = Array.isArray(learning.lastAdvisorSnapshot?.items)
        ? learning.lastAdvisorSnapshot.items.slice(0, 3)
        : [];

    let html = `

        <div class="modal-section">

            <div class="modal-section-title">📋 Метаданные</div>

            <div class="modal-info-grid">

                <div><strong>Название:</strong> ${profile.metadata.name}</div>

                <div><strong>Категория:</strong> ${catNames[profile.metadata.category] || profile.metadata.category}</div>

                <div><strong>Описание:</strong> ${profile.metadata.description || '—'}</div>

                <div><strong>Автор:</strong> ${profile.metadata.author}</div>

                <div><strong>Теги:</strong> ${profile.metadata.tags.join(', ') || '—'}</div>

                <div><strong>Встроенный:</strong> ${profile.metadata.isBuiltin ? 'Да' : 'Нет'}</div>

            </div>

        </div>



        <div class="modal-section">

            <div class="modal-section-title">⚙️ Параметры ректификации</div>

            <div class="modal-info-grid">

                <div><strong>Стабилизация:</strong> ${profile.parameters.rectification.stabilizationMin} мин</div>

                <div><strong>Объём голов:</strong> ${profile.parameters.rectification.headsVolume} мл</div>

                <div><strong>Объём тела:</strong> ${profile.parameters.rectification.bodyVolume} мл</div>

                <div><strong>Объём хвостов:</strong> ${profile.parameters.rectification.tailsVolume} мл</div>

                <div><strong>Скорость голов:</strong> ${profile.parameters.rectification.headsSpeed} мл/ч/кВт</div>

                <div><strong>Скорость тела:</strong> ${profile.parameters.rectification.bodySpeed} мл/ч/кВт</div>

            </div>

        </div>



        <div class="modal-section">

            <div class="modal-section-title">🌡️ Температурные пороги</div>

            <div class="modal-info-grid">

                <div><strong>Макс. куб:</strong> ${profile.parameters.temperatures.maxCube}°C</div>

                <div><strong>Макс. колонна:</strong> ${profile.parameters.temperatures.maxColumn}°C</div>

                <div><strong>Окончание голов:</strong> ${profile.parameters.temperatures.headsEnd}°C</div>

                <div><strong>Начало тела:</strong> ${profile.parameters.temperatures.bodyStart}°C</div>

                <div><strong>Окончание тела:</strong> ${profile.parameters.temperatures.bodyEnd}°C</div>

            </div>

        </div>



        <div class="modal-section">

            <div class="modal-section-title">📊 Статистика использования</div>

            <div class="modal-info-grid">

                <div><strong>Использований:</strong> ${profile.statistics.useCount}</div>

                <div><strong>Средняя длительность:</strong> ${formatProfileMinutes(profile.statistics.avgDuration)}</div>

                <div><strong>Средний выход:</strong> ${profile.statistics.avgYield} мл</div>

                <div><strong>Успешность:</strong> ${profile.statistics.successRate.toFixed(1)}%</div>

                <div><strong>Успешных прогонов:</strong> ${learning.successfulRuns || 0}</div>

                <div><strong>Неуспешных прогонов:</strong> ${learning.failedRuns || 0}</div>

            </div>

        </div>

        <div class="modal-section">

            <div class="modal-section-title">🧠 Learning Loop</div>

            <div class="modal-info-grid">

                <div><strong>Средняя энергия:</strong> ${formatProfileNumber(learning.avgEnergyUsed, 2, ' кВт·ч')}</div>

                <div><strong>Энергия на литр:</strong> ${formatProfileNumber(learning.avgEnergyPerLiter, 2, ' кВт·ч/л')}</div>

                <div><strong>Process health:</strong> ${formatProfileNumber((learning.avgProcessHealth || 0) * 100, 0, '%')}</div>

                <div><strong>Stability index:</strong> ${formatProfileNumber((learning.avgStabilityIndex || 0) * 100, 0, '%')}</div>

                <div><strong>Типовой финал куба:</strong> ${formatProfileNumber(learning.typicalCubeFinalTemp, 1, '°C')}</div>

                <div><strong>Типовой верх колонны:</strong> ${formatProfileNumber(learning.typicalColumnTopFinalTemp, 2, '°C')}</div>

            </div>

        </div>

        <div class="modal-section">

            <div class="modal-section-title">🎯 Последний успешный baseline</div>

            <div class="modal-info-grid">

                <div><strong>ID прогона:</strong> ${lastSuccessfulRun?.id || '—'}</div>

                <div><strong>Длительность:</strong> ${formatProfileMinutes(lastSuccessfulRun?.duration)}</div>

                <div><strong>Выход:</strong> ${lastSuccessfulRun?.totalCollected ? `${lastSuccessfulRun.totalCollected} мл` : '—'}</div>

                <div><strong>Энергия:</strong> ${formatProfileNumber(lastSuccessfulRun?.energyUsed, 2, ' кВт·ч')}</div>

            </div>

            ${advisorItems.length ? `
                <div style="margin-top: 14px;">
                    <strong>Последние рекомендации Run Advisor:</strong>
                    <div style="display: grid; gap: 10px; margin-top: 10px;">
                        ${advisorItems.map((item) => `
                            <div style="padding: 12px; border: 1px solid var(--border-color); border-radius: 10px; background: var(--bg-secondary);">
                                <div style="font-weight: 600; margin-bottom: 6px;">${item.title}</div>
                                <div style="color: var(--text-secondary); margin-bottom: 6px;">${item.detail}</div>
                                <div>${item.action}</div>
                            </div>
                        `).join('')}
                    </div>
                </div>
            ` : '<div style="margin-top: 14px; color: var(--text-secondary);">Для последнего успешного прогона snapshot рекомендаций пока не сохранён.</div>'}

        </div>

    `;



    body.innerHTML = html;

    document.getElementById('profile-view-modal').style.display = 'flex';

}



// Закрыть модальное окно просмотра

export function closeProfileViewModal() {

    document.getElementById('profile-view-modal').style.display = 'none';

    setCurrentProfileId(null);

}



// Быстрая загрузка профиля

export function quickLoadProfile(id) {

    if (!confirm('Загрузить этот профиль в текущие настройки?')) return;



    fetch(`/api/profiles/${id}/load`, {

        method: 'POST'

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                alert('✅ Профиль успешно загружен! Проверьте настройки в разделе "Управление".');

            } else {

                alert('❌ Ошибка загрузки профиля: ' + (data.error || 'Неизвестная ошибка'));

            }

        })

        .catch(error => {

            console.error('Ошибка загрузки профиля:', error);

            alert('❌ Ошибка загрузки профиля');

        });

}



// Загрузка профиля в настройки (из модального окна)

export function loadProfileToSettings() {

    if (!currentProfileId) return;

    closeProfileViewModal();

    quickLoadProfile(currentProfileId);

}



// Удаление профиля

export function deleteProfile(id) {

    if (!confirm('Удалить этот профиль? Действие нельзя отменить.')) return;



    fetch(`/api/profiles/${id}`, {

        method: 'DELETE'

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                loadProfilesList();

                alert('✅ Профиль удалён');

            } else {

                alert('❌ ' + (data.error || 'Ошибка удаления профиля'));

            }

        })

        .catch(error => {

            console.error('Ошибка удаления профиля:', error);

            alert('❌ Ошибка удаления профиля');

        });

}



// Очистка пользовательских профилей

export function clearUserProfiles() {

    if (!confirm('Удалить ВСЕ пользовательские профили? Встроенные рецепты останутся. Действие нельзя отменить!')) return;



    fetch('/api/profiles', {

        method: 'DELETE'

    })

        .then(response => response.json())

        .then(data => {

            if (data.success) {

                loadProfilesList();

                alert('✅ Все пользовательские профили удалены');

            } else {

                alert('❌ Ошибка очистки профилей');

            }

        })

        .catch(error => {

            console.error('Ошибка очистки профилей:', error);

            alert('❌ Ошибка очистки профилей');

        });

}
