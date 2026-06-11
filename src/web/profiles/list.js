// ============================================================================

// PROFILES - Управление профилями процессов

// ============================================================================

import { currentProfileId, setCurrentProfileId } from './state.js';

export { currentProfileId, setCurrentProfileId };

// Загрузка списка профилей

export function loadProfilesList() {

    const listEl = document.getElementById('profiles-list');

    if (!listEl) return;



    listEl.innerHTML = '<p class="info-text">Загрузка профилей...</p>';



    fetch('/api/profiles')
        .then(response => response.json())
        .then(data => {
            const profiles = Array.isArray(data) ? data : (data.profiles || []);
            if (profiles.length > 0) {
                renderProfilesList(profiles);
                updateProfilesStats(profiles);
            } else {
                listEl.innerHTML = '<p class="info-text">📁 Профили не найдены. Создайте первый профиль!</p>';
            }
        })

        .catch(error => {

            console.error('Ошибка загрузки профилей:', error);

            listEl.innerHTML = '<p class="error-text">❌ Ошибка загрузки профилей</p>';

        });

}



// Отрисовка списка профилей

export function renderProfilesList(profiles) {

    const listEl = document.getElementById('profiles-list');

    const filter = document.getElementById('profile-filter-category').value;



    // Применить фильтр

    const filtered = filter === 'all'

        ? profiles

        : profiles.filter(p => p.category === filter);



    if (filtered.length === 0) {

        listEl.innerHTML = '<p class="info-text">📁 Профили не найдены для выбранной категории</p>';

        return;

    }



    let html = '';

    filtered.forEach(profile => {

        html += renderProfileItem(profile);

    });



    listEl.innerHTML = html;

}



// Отрисовка элемента профиля

export function renderProfileItem(profile) {

    const categoryIcons = {

        'rectification': '🌀',

        'distillation': '🔥',

        'mashing': '🌾'

    };



    const categoryNames = {

        'rectification': 'Ректификация',

        'distillation': 'Дистилляция',

        'mashing': 'Затирка'

    };



    const icon = categoryIcons[profile.category] || '📁';

    const catName = categoryNames[profile.category] || profile.category;

    const builtinBadge = profile.isBuiltin ? '<span style="background: #2196F3; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.75em; margin-left: 8px;">Встроенный</span>' : '';



    const lastUsed = profile.lastUsed > 0

        ? new Date(profile.lastUsed * 1000).toLocaleDateString('ru-RU')

        : 'Не использовался';

    const successText = Number(profile.successfulRuns || 0) > 0
        ? ` • Успешность: ${Number(profile.successRate || 0).toFixed(0)}%`
        : '';



    return `

        <div class="profile-item" style="background: var(--bg-primary); padding: 15px; margin-bottom: 10px; border-radius: 8px; border-left: 4px solid var(--accent-color);">

            <div style="display: flex; justify-content: space-between; align-items: start; margin-bottom: 10px;">

                <div style="flex: 1;">

                    <div style="font-weight: 600; font-size: 1.1em; margin-bottom: 5px;">

                        ${icon} ${profile.name}${builtinBadge}

                    </div>

                    <div style="color: var(--text-secondary); font-size: 0.9em;">

                        ${catName} • Использований: ${profile.useCount}${successText} • Последнее: ${lastUsed}

                    </div>

                </div>

                <div style="display: flex; gap: 5px;">

                    <button class="btn-icon" onclick="viewProfile('${profile.id}')" title="Просмотр">👁️</button>

                    <button class="btn-icon btn-success" onclick="quickLoadProfile('${profile.id}')" title="Загрузить">📥</button>

                    ${!profile.isBuiltin ? `<button class="btn-icon" onclick="showEditProfileModal('${profile.id}')" title="Редактировать">✏️</button>` : ''}

                    <button class="btn-icon" onclick="exportProfile('${profile.id}')" title="Экспорт">📤</button>

                    ${!profile.isBuiltin ? `<button class="btn-icon btn-danger" onclick="deleteProfile('${profile.id}')" title="Удалить">🗑️</button>` : ''}

                </div>

            </div>

        </div>

    `;

}



// Обновление статистики профилей

export function updateProfilesStats(profiles) {

    document.getElementById('prof-stat-total').textContent = profiles.length;



    const builtin = profiles.filter(p => p.isBuiltin).length;

    const user = profiles.length - builtin;



    document.getElementById('prof-stat-builtin').textContent = builtin;

    document.getElementById('prof-stat-user').textContent = user;



    // Самый используемый

    if (profiles.length > 0) {

        const mostUsed = profiles.reduce((prev, current) =>

            (prev.useCount > current.useCount) ? prev : current

        );

        document.getElementById('prof-stat-popular').textContent =

            mostUsed.useCount > 0 ? mostUsed.name : '—';

    } else {

        document.getElementById('prof-stat-popular').textContent = '—';

    }

}
