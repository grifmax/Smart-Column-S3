// ============================================================================
// Профили процессов - JavaScript модуль
// ============================================================================

let profilesList = [];
let currentProfileId = null;

// Загрузка списка профилей
async function loadProfiles() {
    try {
        const response = await fetch('/api/profiles');
        if (!response.ok) {
            throw new Error('Failed to load profiles');
        }
        
        const data = await response.json();
        profilesList = data.profiles || [];
        renderProfilesList();
    } catch (error) {
        console.error('Error loading profiles:', error);
        document.getElementById('profiles-list').innerHTML = 
            '<div class="error">Ошибка загрузки профилей: ' + error.message + '</div>';
    }
}

// Отрисовка списка профилей
function renderProfilesList() {
    const container = document.getElementById('profiles-list');
    const categoryFilter = document.getElementById('profile-category-filter').value;
    const searchQuery = document.getElementById('profile-search').value.toLowerCase();
    
    // Фильтрация
    let filtered = profilesList.filter(profile => {
        const matchCategory = !categoryFilter || profile.category === categoryFilter;
        const matchSearch = !searchQuery || profile.name.toLowerCase().includes(searchQuery);
        return matchCategory && matchSearch;
    });
    
    if (filtered.length === 0) {
        container.innerHTML = '<div class="empty-state">Профили не найдены</div>';
        return;
    }
    
    // Сортировка по использованию (чаще используемые сверху)
    filtered.sort((a, b) => b.useCount - a.useCount);
    
    let html = '<div class="profiles-grid">';
    filtered.forEach(profile => {
        const categoryIcons = {
            'rectification': '⚗️',
            'distillation': '🔥',
            'mashing': '🌾'
        };
        const icon = categoryIcons[profile.category] || '📋';
        const lastUsed = profile.lastUsed ? 
            new Date(profile.lastUsed * 1000).toLocaleDateString('ru-RU') : 'Никогда';
        
        html += `
            <div class="profile-card" data-profile-id="${profile.id}">
                <div class="profile-card-header">
                    <span class="profile-icon">${icon}</span>
                    <h4>${escapeHtml(profile.name)}</h4>
                    ${profile.isBuiltin ? '<span class="badge badge-builtin">Встроенный</span>' : ''}
                </div>
                <div class="profile-card-body">
                    <div class="profile-info">
                        <span class="profile-category">${getCategoryName(profile.category)}</span>
                        <span class="profile-uses">Использований: ${profile.useCount}</span>
                        <span class="profile-last-used">Последнее: ${lastUsed}</span>
                    </div>
                </div>
                <div class="profile-card-actions">
                    <button class="btn btn-sm btn-primary load-profile-btn" data-profile-id="${profile.id}">
                        Загрузить
                    </button>
                    ${!profile.isBuiltin ? `
                        <button class="btn btn-sm btn-secondary edit-profile-btn" data-profile-id="${profile.id}">
                            Редактировать
                        </button>
                        <button class="btn btn-sm btn-danger delete-profile-btn" data-profile-id="${profile.id}">
                            Удалить
                        </button>
                    ` : ''}
                </div>
            </div>
        `;
    });
    html += '</div>';
    
    container.innerHTML = html;
    
    // Привязка обработчиков событий
    attachProfileEventHandlers();
}

// Привязка обработчиков событий для карточек профилей
function attachProfileEventHandlers() {
    // Кнопка загрузки профиля
    document.querySelectorAll('.load-profile-btn').forEach(btn => {
        btn.addEventListener('click', async (e) => {
            const profileId = e.target.getAttribute('data-profile-id');
            await loadProfile(profileId);
        });
    });
    
    // Кнопка редактирования
    document.querySelectorAll('.edit-profile-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const profileId = e.target.getAttribute('data-profile-id');
            editProfile(profileId);
        });
    });
    
    // Кнопка удаления
    document.querySelectorAll('.delete-profile-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const profileId = e.target.getAttribute('data-profile-id');
            deleteProfileConfirm(profileId);
        });
    });
}

// Загрузка профиля в систему
async function loadProfile(profileId) {
    try {
        const response = await fetch(`/api/profiles/${profileId}/load`, {
            method: 'POST'
        });
        
        if (!response.ok) {
            throw new Error('Failed to load profile');
        }
        
        const data = await response.json();
        if (data.success) {
            showNotification('Профиль успешно загружен', 'success');
            // Перезагрузить настройки если нужно
            setTimeout(() => location.reload(), 1000);
        }
    } catch (error) {
        console.error('Error loading profile:', error);
        showNotification('Ошибка загрузки профиля: ' + error.message, 'error');
    }
}

// Редактирование профиля
async function editProfile(profileId) {
    try {
        const response = await fetch(`/api/profiles/${profileId}`);
        if (!response.ok) {
            throw new Error('Failed to load profile');
        }
        
        const profile = await response.json();
        currentProfileId = profileId;
        showProfileModal(profile, true);
    } catch (error) {
        console.error('Error loading profile for edit:', error);
        showNotification('Ошибка загрузки профиля: ' + error.message, 'error');
    }
}

// Удаление профиля с подтверждением
async function deleteProfileConfirm(profileId) {
    const profile = profilesList.find(p => p.id === profileId);
    if (!profile) return;
    
    if (confirm(`Вы уверены, что хотите удалить профиль "${profile.name}"?`)) {
        await deleteProfile(profileId);
    }
}

// Удаление профиля
async function deleteProfile(profileId) {
    try {
        const response = await fetch(`/api/profiles/${profileId}`, {
            method: 'DELETE'
        });
        
        if (!response.ok) {
            throw new Error('Failed to delete profile');
        }
        
        const data = await response.json();
        if (data.success) {
            showNotification('Профиль удалён', 'success');
            await loadProfiles(); // Перезагрузить список
        }
    } catch (error) {
        console.error('Error deleting profile:', error);
        showNotification('Ошибка удаления профиля: ' + error.message, 'error');
    }
}

// Показать модальное окно профиля
function showProfileModal(profile = null, isEdit = false) {
    const modal = document.getElementById('profile-modal');
    const title = document.getElementById('profile-modal-title');
    const saveBtn = document.getElementById('profile-save-btn');
    const loadBtn = document.getElementById('profile-load-btn');
    const deleteBtn = document.getElementById('profile-delete-btn');
    const formContainer = document.getElementById('profile-form-container');
    
    if (profile) {
        title.textContent = isEdit ? 'Редактировать профиль' : 'Просмотр профиля';
        loadBtn.style.display = isEdit ? 'none' : 'inline-block';
        deleteBtn.style.display = profile.isBuiltin ? 'none' : 'inline-block';
        currentProfileId = profile.id;
        
        // Заполнить форму данными профиля
        formContainer.innerHTML = generateProfileForm(profile);
    } else {
        title.textContent = 'Создать профиль';
        loadBtn.style.display = 'none';
        deleteBtn.style.display = 'none';
        currentProfileId = null;
        
        // Пустая форма
        formContainer.innerHTML = generateProfileForm(null);
    }
    
    modal.style.display = 'block';
}

// Генерация HTML формы профиля
function generateProfileForm(profile) {
    // Упрощённая форма - только основные поля
    return `
        <form id="profile-form">
            <div class="form-group">
                <label for="profile-name">Название профиля:</label>
                <input type="text" id="profile-name" required 
                       value="${profile ? escapeHtml(profile.metadata.name) : ''}">
            </div>
            <div class="form-group">
                <label for="profile-description">Описание:</label>
                <textarea id="profile-description" rows="3">${profile ? escapeHtml(profile.metadata.description || '') : ''}</textarea>
            </div>
            <div class="form-group">
                <label for="profile-category">Категория:</label>
                <select id="profile-category" required>
                    <option value="rectification" ${profile && profile.metadata.category === 'rectification' ? 'selected' : ''}>
                        Ректификация
                    </option>
                    <option value="distillation" ${profile && profile.metadata.category === 'distillation' ? 'selected' : ''}>
                        Дистилляция
                    </option>
                    <option value="mashing" ${profile && profile.metadata.category === 'mashing' ? 'selected' : ''}>
                        Затирка
                    </option>
                </select>
            </div>
            <div class="profile-note">
                <strong>Примечание:</strong> Полная настройка профиля будет доступна в следующей версии.
                Сейчас вы можете сохранить текущие настройки как профиль через раздел "Настройки".
            </div>
        </form>
    `;
}

// Сохранение профиля
async function saveProfile() {
    const form = document.getElementById('profile-form');
    if (!form || !form.checkValidity()) {
        form.reportValidity();
        return;
    }
    
    const profileData = {
        metadata: {
            name: document.getElementById('profile-name').value,
            description: document.getElementById('profile-description').value,
            category: document.getElementById('profile-category').value
        },
        parameters: {
            // TODO: Заполнить параметры из текущих настроек
            mode: document.getElementById('profile-category').value
        }
    };
    
    try {
        let response;
        if (currentProfileId) {
            // Обновление существующего профиля
            response = await fetch(`/api/profiles/${currentProfileId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(profileData)
            });
        } else {
            // Создание нового профиля
            response = await fetch('/api/profiles', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(profileData)
            });
        }
        
        if (!response.ok) {
            throw new Error('Failed to save profile');
        }
        
        const data = await response.json();
        if (data.success) {
            showNotification(currentProfileId ? 'Профиль обновлён' : 'Профиль создан', 'success');
            closeProfileModal();
            await loadProfiles(); // Перезагрузить список
        }
    } catch (error) {
        console.error('Error saving profile:', error);
        showNotification('Ошибка сохранения профиля: ' + error.message, 'error');
    }
}

// Закрытие модального окна
function closeProfileModal() {
    document.getElementById('profile-modal').style.display = 'none';
    currentProfileId = null;
}

// Вспомогательные функции
function getCategoryName(category) {
    const names = {
        'rectification': 'Ректификация',
        'distillation': 'Дистилляция',
        'mashing': 'Затирка'
    };
    return names[category] || category;
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function showNotification(message, type = 'info') {
    // Используем существующую функцию показа уведомлений если есть
    if (typeof window.showNotification === 'function') {
        window.showNotification(message, type);
    } else {
        alert(message);
    }
}

// Инициализация при загрузке страницы
document.addEventListener('DOMContentLoaded', () => {
    // Загрузка профилей при открытии раздела
    const profilesSection = document.getElementById('profiles');
    if (profilesSection) {
        const observer = new MutationObserver((mutations) => {
            if (profilesSection.classList.contains('active')) {
                loadProfiles();
            }
        });
        observer.observe(profilesSection, { attributes: true, attributeFilter: ['class'] });
    }
    
    // Обработчики модального окна
    const modal = document.getElementById('profile-modal');
    const closeBtn = document.getElementById('profile-modal-close');
    const cancelBtn = document.getElementById('profile-cancel-btn');
    const saveBtn = document.getElementById('profile-save-btn');
    const loadBtn = document.getElementById('profile-load-btn');
    const deleteBtn = document.getElementById('profile-delete-btn');
    const createBtn = document.getElementById('create-profile-btn');
    
    if (closeBtn) closeBtn.addEventListener('click', closeProfileModal);
    if (cancelBtn) cancelBtn.addEventListener('click', closeProfileModal);
    if (saveBtn) saveBtn.addEventListener('click', saveProfile);
    if (loadBtn) loadBtn.addEventListener('click', async () => {
        if (currentProfileId) {
            await loadProfile(currentProfileId);
            closeProfileModal();
        }
    });
    if (deleteBtn) deleteBtn.addEventListener('click', async () => {
        if (currentProfileId) {
            await deleteProfileConfirm(currentProfileId);
            closeProfileModal();
        }
    });
    if (createBtn) createBtn.addEventListener('click', () => showProfileModal());
    
    // Фильтры
    const categoryFilter = document.getElementById('profile-category-filter');
    const searchInput = document.getElementById('profile-search');
    
    if (categoryFilter) {
        categoryFilter.addEventListener('change', renderProfilesList);
    }
    if (searchInput) {
        searchInput.addEventListener('input', renderProfilesList);
    }
    
    // Закрытие по клику вне модального окна
    if (modal) {
        window.addEventListener('click', (e) => {
            if (e.target === modal) {
                closeProfileModal();
            }
        });
    }
});

