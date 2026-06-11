import { loadProfilesList } from './list.js';

const PROFILES_SNAPSHOT_SCHEMA = 'smart-column-profiles-snapshot-v1';

let importPayload = null;

function safeFileNamePart(value, fallback = 'profile') {
    const normalized = String(value || '')
        .trim()
        .replace(/[\\/:*?"<>|]+/g, '_')
        .replace(/\s+/g, '_');
    return normalized || fallback;
}

function downloadJsonFile(data, filename) {
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
}

async function fetchVersionMeta() {
    try {
        const response = await fetch('/api/version');
        if (!response.ok) {
            return {};
        }
        const payload = await response.json();
        return {
            firmwareVersion: payload?.firmware || payload?.version || '',
            board: payload?.board || payload?.chipModel || ''
        };
    } catch {
        return {};
    }
}

function normalizeImportPayload(payload) {
    if (Array.isArray(payload)) {
        return payload;
    }

    if (payload && Array.isArray(payload.profiles)) {
        return payload.profiles;
    }

    if (payload && payload.profile && payload.profile.metadata && payload.profile.parameters) {
        return [payload.profile];
    }

    if (payload && payload.metadata && payload.parameters) {
        return [payload];
    }

    throw new Error('Неверный формат JSON для импорта профилей');
}

function buildImportPreview(payload) {
    const profiles = normalizeImportPayload(payload);
    const names = profiles
        .slice(0, 5)
        .map((profile) => `• ${profile?.metadata?.name || profile?.name || 'Без имени'}`);

    return {
        profiles,
        summary: [
            `Профилей к импорту: ${profiles.length}`,
            payload?.schema === PROFILES_SNAPSHOT_SCHEMA ? `Snapshot: ${payload.schema}` : '',
            ...names,
            profiles.length > 5 ? `• …ещё ${profiles.length - 5}` : ''
        ].filter(Boolean).join('\n')
    };
}

function updateImportPreview(text = '', visible = false) {
    const preview = document.getElementById('import-preview');
    const previewText = document.getElementById('import-preview-text');
    if (!preview || !previewText) {
        return;
    }

    previewText.textContent = text;
    preview.style.display = visible ? 'block' : 'none';
}

// Экспорт одного профиля
export async function exportProfile(id) {
    try {
        const [profileResponse, versionMeta] = await Promise.all([
            fetch(`/api/profiles/${id}/export`),
            fetchVersionMeta()
        ]);

        if (!profileResponse.ok) {
            throw new Error('Не удалось экспортировать профиль');
        }

        const profile = await profileResponse.json();
        const fileName = `profile_${safeFileNamePart(profile?.metadata?.name, id)}_${id}.json`;

        downloadJsonFile({
            schema: PROFILES_SNAPSHOT_SCHEMA,
            exportedAt: new Date().toISOString(),
            meta: {
                scope: 'single-profile',
                ...versionMeta
            },
            profile
        }, fileName);
    } catch (error) {
        console.error('Ошибка экспорта профиля:', error);
        alert('❌ Ошибка экспорта профиля');
    }
}

// Экспорт всех профилей
export async function exportAllProfiles() {
    const includeBuiltin = confirm('Включить встроенные рецепты в экспорт?');

    try {
        const [profilesResponse, versionMeta] = await Promise.all([
            fetch(`/api/profiles/export${includeBuiltin ? '?includeBuiltin=true' : ''}`),
            fetchVersionMeta()
        ]);

        if (!profilesResponse.ok) {
            throw new Error('Не удалось экспортировать профили');
        }

        const profiles = await profilesResponse.json();
        if (!Array.isArray(profiles) || profiles.length === 0) {
            alert('Нет профилей для экспорта');
            return;
        }

        const timestamp = new Date().toISOString().split('T')[0];
        downloadJsonFile({
            schema: PROFILES_SNAPSHOT_SCHEMA,
            exportedAt: new Date().toISOString(),
            meta: {
                scope: 'profiles-batch',
                includeBuiltin,
                profileCount: profiles.length,
                ...versionMeta
            },
            profiles
        }, `profiles_export_${timestamp}.json`);

        alert(`✅ Экспортировано профилей: ${profiles.length}`);
    } catch (error) {
        console.error('Ошибка экспорта профилей:', error);
        alert('❌ Ошибка экспорта профилей');
    }
}

// Показать модальное окно импорта
export function showImportModal() {
    importPayload = null;
    document.getElementById('import-file-input').value = '';
    document.getElementById('import-btn').disabled = true;
    updateImportPreview('', false);
    document.getElementById('profile-import-modal').style.display = 'flex';

    document.getElementById('import-file-input').onchange = function (event) {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = function (loadEvent) {
            try {
                const parsed = JSON.parse(loadEvent.target.result);
                const preview = buildImportPreview(parsed);
                importPayload = preview.profiles;
                updateImportPreview(preview.summary, true);
                document.getElementById('import-btn').disabled = preview.profiles.length === 0;
            } catch (error) {
                console.error('Ошибка чтения файла профилей:', error);
                importPayload = null;
                updateImportPreview('', false);
                document.getElementById('import-btn').disabled = true;
                alert(`❌ Ошибка чтения файла: ${error.message || 'Неверный формат JSON'}`);
            }
        };
        reader.readAsText(file);
    };
}

// Закрыть модальное окно импорта
export function closeImportModal() {
    document.getElementById('profile-import-modal').style.display = 'none';
    importPayload = null;
}

// Выполнить импорт профилей
export function doImportProfiles() {
    if (!Array.isArray(importPayload) || importPayload.length === 0) {
        alert('Выберите корректный файл профилей для импорта');
        return;
    }

    fetch('/api/profiles/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(importPayload)
    })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                const importedCount = Number(data.imported ?? data.count ?? 0);
                closeImportModal();
                loadProfilesList();
                alert(`✅ Импортировано профилей: ${importedCount}`);
            } else {
                alert('❌ Ошибка импорта: ' + (data.error || 'Неизвестная ошибка'));
            }
        })
        .catch(error => {
            console.error('Ошибка импорта профилей:', error);
            alert('❌ Ошибка импорта профилей');
        });
}
