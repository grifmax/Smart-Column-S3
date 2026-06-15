// ============================================================================
// PROFILES - Управление профилями процессов
// ============================================================================

import { currentProfileId, setCurrentProfileId } from './state.js';
import { getProfileCompatibilityBadge } from './compat.js';

export { currentProfileId, setCurrentProfileId };

function escapeHtml(value) {
    return String(value ?? '')
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function normalizeSearchText(profile) {
    const tags = Array.isArray(profile?.tags) ? profile.tags.join(' ') : '';
    return [
        profile?.name,
        profile?.description,
        profile?.category,
        profile?.author,
        profile?.id,
        tags
    ]
        .filter(Boolean)
        .join(' ')
        .toLowerCase();
}

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
    const filter = document.getElementById('profile-filter-category')?.value || 'all';
    const query = String(document.getElementById('profile-search')?.value || '').trim().toLowerCase();

    const filtered = profiles.filter((profile) => {
        const matchesCategory = filter === 'all' || profile.category === filter;
        const matchesSearch = !query || normalizeSearchText(profile).includes(query);
        return matchesCategory && matchesSearch;
    });

    if (filtered.length === 0) {
        listEl.innerHTML = query
            ? '<p class="info-text">🔎 По этому запросу профили не найдены</p>'
            : '<p class="info-text">📁 Профили не найдены для выбранной категории</p>';
        return;
    }

    listEl.innerHTML = filtered.map(renderProfileItem).join('');
}

// Отрисовка элемента профиля
export function renderProfileItem(profile) {
    const categoryIcons = {
        rectification: '🌀',
        distillation: '🔥',
        mashing: '🌾'
    };

    const categoryNames = {
        rectification: 'Ректификация',
        distillation: 'Дистилляция',
        mashing: 'Затирка'
    };

    const icon = categoryIcons[profile.category] || '📁';
    const catName = categoryNames[profile.category] || profile.category;
    const builtinBadge = profile.isBuiltin
        ? '<span style="background: #2196F3; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.75em; margin-left: 8px;">Встроенный</span>'
        : '';
    const lastUsed = profile.lastUsed > 0
        ? new Date(profile.lastUsed * 1000).toLocaleDateString('ru-RU')
        : 'Не использовался';
    const successText = Number(profile.successfulRuns || 0) > 0
        ? ` • Успешность: ${Number(profile.successRate || 0).toFixed(0)}%`
        : '';
    const description = String(profile.description || '').trim();
    const tags = Array.isArray(profile.tags) ? profile.tags.filter(Boolean) : [];
    const compatibility = getProfileCompatibilityBadge(profile);
    const compatibilityBadge = compatibility.detail
        ? `
            <div style="margin-top: 8px;">
                <span style="display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 999px; font-size: 0.8em; border: 1px solid ${compatibility.tone === 'warn' ? 'rgba(216,119,6,0.35)' : 'var(--border-color)'}; background: ${compatibility.tone === 'warn' ? 'rgba(245,158,11,0.12)' : 'var(--bg-secondary)'}; color: var(--text-primary);">
                    ${escapeHtml(compatibility.label)}
                </span>
                <div style="margin-top: 6px; color: var(--text-secondary); font-size: 0.85em; line-height: 1.45;">
                    ${escapeHtml(compatibility.detail)}
                </div>
            </div>
        `
        : (compatibility.tone === 'good'
            ? `
                <div style="margin-top: 8px;">
                    <span style="display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 999px; font-size: 0.8em; border: 1px solid rgba(34,197,94,0.25); background: rgba(34,197,94,0.12); color: var(--text-primary);">
                        ${escapeHtml(compatibility.label)}
                    </span>
                </div>
            `
            : '');

    return `
        <div class="profile-item" style="background: var(--bg-primary); padding: 15px; margin-bottom: 10px; border-radius: 8px; border-left: 4px solid var(--accent-color);">
            <div style="display: flex; justify-content: space-between; align-items: start; gap: 12px; margin-bottom: 10px; flex-wrap: wrap;">
                <div style="flex: 1; min-width: 240px;">
                    <div style="font-weight: 600; font-size: 1.1em; margin-bottom: 5px;">
                        ${icon} ${escapeHtml(profile.name)}${builtinBadge}
                    </div>
                    <div style="color: var(--text-secondary); font-size: 0.9em;">
                        ${catName} • Использований: ${profile.useCount}${successText} • Последнее: ${lastUsed}
                    </div>
                    ${description ? `
                        <div style="margin-top: 8px; color: var(--text-secondary); line-height: 1.45;">
                            ${escapeHtml(description)}
                        </div>
                    ` : ''}
                    ${compatibilityBadge}
                    ${tags.length ? `
                        <div style="display: flex; gap: 6px; flex-wrap: wrap; margin-top: 10px;">
                            ${tags.slice(0, 4).map((tag) => `
                                <span style="padding: 4px 8px; border-radius: 999px; background: var(--bg-secondary); border: 1px solid var(--border-color); font-size: 0.8em; color: var(--text-secondary);">
                                    #${escapeHtml(tag)}
                                </span>
                            `).join('')}
                        </div>
                    ` : ''}
                </div>
                <div style="display: flex; gap: 5px; flex-wrap: wrap; justify-content: flex-end;">
                    <button class="btn-icon" onclick="viewProfile('${profile.id}')" title="Просмотр">👁️</button>
                    <button class="btn-icon btn-success" onclick="quickLoadProfile('${profile.id}')" title="Загрузить">📥</button>
                    <button class="btn-icon" onclick="showDuplicateProfileModal('${profile.id}')" title="Сделать копию">📄</button>
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
