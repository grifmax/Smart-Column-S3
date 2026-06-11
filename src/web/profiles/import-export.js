import { loadProfilesList } from './list.js';

const PROFILES_SNAPSHOT_SCHEMA = 'smart-column-profiles-snapshot-v1';
const MMHG_PER_HPA = 0.75006156;

let importPayload = null;
let importContextPromise = null;

function safeFileNamePart(value, fallback = 'profile') {
    const normalized = String(value || '')
        .trim()
        .replace(/[\\/:*?"<>|]+/g, '_')
        .replace(/\s+/g, '_');
    return normalized || fallback;
}

function escapeHtml(value) {
    return String(value ?? '')
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
}

function toFiniteNumber(value, fallback = 0) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? numeric : fallback;
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

function formatNumber(value, digits = 1, suffix = '') {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? `${numeric.toFixed(digits)}${suffix}` : '—';
}

function formatDateTime(value) {
    if (!value) return '—';
    const date = new Date(value);
    return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleString('ru-RU');
}

function toMmHgFromStatus(statusPayload) {
    const hpa = Number(statusPayload?.pressure?.atm ?? statusPayload?.p_atm ?? 0);
    if (!Number.isFinite(hpa) || hpa <= 0) {
        return 0;
    }
    return hpa * MMHG_PER_HPA;
}

async function fetchJsonOrEmpty(url) {
    try {
        const response = await fetch(url);
        if (!response.ok) {
            return {};
        }
        return await response.json();
    } catch {
        return {};
    }
}

async function fetchImportContext() {
    const [versionPayload, equipmentPayload, safetyPayload, statusPayload] = await Promise.all([
        fetchJsonOrEmpty('/api/version'),
        fetchJsonOrEmpty('/api/settings/equipment'),
        fetchJsonOrEmpty('/api/settings/safety'),
        fetchJsonOrEmpty('/api/status')
    ]);

    return {
        firmwareVersion: versionPayload?.firmware || versionPayload?.version || '',
        board: versionPayload?.board || versionPayload?.chipModel || '',
        heaterPowerW: toFiniteNumber(equipmentPayload?.heaterPowerW, 0),
        columnHeightMm: toFiniteNumber(equipmentPayload?.columnHeightMm, 0),
        cubeVolumeL: toFiniteNumber(equipmentPayload?.cubeVolumeL, 0),
        packingType: String(equipmentPayload?.packingType || '').trim(),
        packingCoeff: toFiniteNumber(equipmentPayload?.packingCoeff, 0),
        pressureMaxMmHg: toFiniteNumber(safetyPayload?.pressureMaxMmHg, 0),
        currentPressureMmHg: toMmHgFromStatus(statusPayload)
    };
}

function buildSnapshotMeta(versionMeta, deviceContext, extra = {}) {
    return {
        firmwareVersion: versionMeta?.firmwareVersion || '',
        board: versionMeta?.board || '',
        deviceContext: {
            heaterPowerW: deviceContext?.heaterPowerW || 0,
            columnHeightMm: deviceContext?.columnHeightMm || 0,
            cubeVolumeL: deviceContext?.cubeVolumeL || 0,
            packingType: deviceContext?.packingType || '',
            packingCoeff: deviceContext?.packingCoeff || 0,
            pressureMaxMmHg: deviceContext?.pressureMaxMmHg || 0,
            currentPressureMmHg: deviceContext?.currentPressureMmHg || 0
        },
        ...extra
    };
}

function normalizeImportEnvelope(payload) {
    if (Array.isArray(payload)) {
        return {
            schema: '',
            meta: {},
            profiles: payload
        };
    }

    if (payload && Array.isArray(payload.profiles)) {
        return {
            schema: String(payload.schema || ''),
            meta: {
                ...(payload.meta && typeof payload.meta === 'object' ? payload.meta : {}),
                exportedAt: payload.exportedAt || payload?.meta?.exportedAt || ''
            },
            profiles: payload.profiles
        };
    }

    if (payload && payload.profile && payload.profile.metadata && payload.profile.parameters) {
        return {
            schema: String(payload.schema || ''),
            meta: {
                ...(payload.meta && typeof payload.meta === 'object' ? payload.meta : {}),
                exportedAt: payload.exportedAt || payload?.meta?.exportedAt || ''
            },
            profiles: [payload.profile]
        };
    }

    if (payload && payload.metadata && payload.parameters) {
        return {
            schema: '',
            meta: {},
            profiles: [payload]
        };
    }

    throw new Error('Неверный формат JSON для импорта профилей');
}

function compareRelativeDiff(currentValue, importedValue) {
    const current = Number(currentValue);
    const imported = Number(importedValue);
    if (!(current > 0) || !(imported > 0)) {
        return 0;
    }
    return Math.abs(imported - current) / current;
}

function buildCompatibilityMessages(profile, context, snapshotMeta) {
    const validation = profile?.validation && typeof profile.validation === 'object'
        ? profile.validation
        : {};
    const snapshotContext = snapshotMeta?.deviceContext && typeof snapshotMeta.deviceContext === 'object'
        ? snapshotMeta.deviceContext
        : {};

    const title = profile?.metadata?.name || profile?.name || 'Без имени';
    const warnings = [];
    const notes = [];
    let tone = 'good';

    if (!profile?.metadata?.name || !profile?.parameters) {
        return {
            title,
            tone: 'danger',
            warnings: ['Профиль неполный: не хватает имени или основных параметров.'],
            notes: []
        };
    }

    if (!validation.validatedAt) {
        notes.push('У профиля нет validation context успешного baseline, совместимость оценена только по уставкам.');
    }

    const referenceHeater = validation.heaterPowerW || snapshotContext.heaterPowerW || 0;
    const heaterDiff = compareRelativeDiff(context.heaterPowerW, referenceHeater);
    if (heaterDiff >= 0.25) {
        tone = 'warning';
        warnings.push(`Мощность: профиль валидирован под ${formatNumber(referenceHeater, 0, ' Вт')}, у вас ${formatNumber(context.heaterPowerW, 0, ' Вт')}.`);
    } else if (heaterDiff >= 0.1) {
        notes.push(`Мощность установки отличается: baseline ${formatNumber(referenceHeater, 0, ' Вт')} vs текущее ${formatNumber(context.heaterPowerW, 0, ' Вт')}.`);
    }

    const referenceHeight = validation.columnHeightMm || snapshotContext.columnHeightMm || 0;
    const heightDiff = Math.abs(toFiniteNumber(referenceHeight, 0) - toFiniteNumber(context.columnHeightMm, 0));
    if (referenceHeight > 0 && context.columnHeightMm > 0) {
        if (heightDiff >= 150) {
            tone = 'warning';
            warnings.push(`Высота колонны отличается на ${formatNumber(heightDiff, 0, ' мм')} (${formatNumber(referenceHeight, 0, ' мм')} в baseline).`);
        } else if (heightDiff >= 50) {
            notes.push(`Высота колонны немного отличается: baseline ${formatNumber(referenceHeight, 0, ' мм')} vs текущее ${formatNumber(context.columnHeightMm, 0, ' мм')}.`);
        }
    }

    const referencePackingType = String(validation.packingType || snapshotContext.packingType || '').trim();
    if (referencePackingType && context.packingType && referencePackingType !== context.packingType) {
        tone = 'warning';
        warnings.push(`Насадка не совпадает: baseline ${referencePackingType}, сейчас ${context.packingType}.`);
    }

    const referencePackingCoeff = validation.packingCoeff || snapshotContext.packingCoeff || 0;
    const packingCoeffDiff = Math.abs(toFiniteNumber(referencePackingCoeff, 0) - toFiniteNumber(context.packingCoeff, 0));
    if (referencePackingCoeff > 0 && context.packingCoeff > 0) {
        if (packingCoeffDiff >= 0.4) {
            tone = 'warning';
            warnings.push(`Коэффициент насадки заметно отличается: baseline ${formatNumber(referencePackingCoeff, 1)} vs текущее ${formatNumber(context.packingCoeff, 1)}.`);
        } else if (packingCoeffDiff >= 0.15) {
            notes.push(`Коэффициент насадки немного отличается: baseline ${formatNumber(referencePackingCoeff, 1)} vs текущее ${formatNumber(context.packingCoeff, 1)}.`);
        }
    }

    const referenceFeedVolume = validation.feedVolumeL || 0;
    if (referenceFeedVolume > 0 && context.cubeVolumeL > 0 && referenceFeedVolume > context.cubeVolumeL) {
        tone = 'danger';
        warnings.push(`Baseline был на ${formatNumber(referenceFeedVolume, 1, ' л')}, что больше текущего куба ${formatNumber(context.cubeVolumeL, 1, ' л')}.`);
    } else if (referenceFeedVolume > 0 && context.cubeVolumeL > 0 && referenceFeedVolume > context.cubeVolumeL * 0.85) {
        tone = tone === 'good' ? 'warning' : tone;
        warnings.push(`Профиль рассчитан на почти полный куб: baseline ${formatNumber(referenceFeedVolume, 1, ' л')} при лимите ${formatNumber(context.cubeVolumeL, 1, ' л')}.`);
    }

    const pressureDiff = Math.abs(toFiniteNumber(validation.atmosphereMmHg, 0) - toFiniteNumber(context.currentPressureMmHg, 0));
    if (validation.atmosphereMmHg > 0 && context.currentPressureMmHg > 0 && pressureDiff >= 12) {
        notes.push(`Атмосферное давление заметно отличается: baseline ${formatNumber(validation.atmosphereMmHg, 1, ' мм рт.ст.')} vs сейчас ${formatNumber(context.currentPressureMmHg, 1, ' мм рт.ст.')}. Барокоррекция поможет, но запуск всё равно лучше перепроверить.`);
    }

    const pressureLimitDiff = Math.abs(toFiniteNumber(profile?.parameters?.safety?.pressureMax, 0) - toFiniteNumber(context.pressureMaxMmHg, 0));
    if (context.pressureMaxMmHg > 0 && pressureLimitDiff >= 20) {
        notes.push(`Лимит давления в профиле отличается от текущего safety-порога устройства.`);
    }

    return { title, tone, warnings, notes };
}

function buildCategorySummary(profiles) {
    const labels = {
        rectification: 'ректификация',
        distillation: 'дистилляция',
        mashing: 'затирание'
    };
    const counters = new Map();
    profiles.forEach((profile) => {
        const key = String(profile?.metadata?.category || profile?.parameters?.mode || 'other').trim() || 'other';
        counters.set(key, (counters.get(key) || 0) + 1);
    });

    return Array.from(counters.entries())
        .map(([key, count]) => `${labels[key] || key}: ${count}`)
        .join(' • ');
}

function renderCompatibilityCard(analysis) {
    const toneClass = analysis.tone === 'danger'
        ? 'is-danger'
        : analysis.tone === 'warning'
            ? 'is-warning'
            : 'is-good';
    const badgeText = analysis.tone === 'danger'
        ? 'Риск'
        : analysis.tone === 'warning'
            ? 'Проверить'
            : 'Совместим';

    return `
        <div class="profile-import-card ${toneClass}">
            <div class="profile-import-card-head">
                <strong>${escapeHtml(analysis.title)}</strong>
                <span class="profile-import-badge ${toneClass}">${badgeText}</span>
            </div>
            ${analysis.warnings.length ? `
                <ul class="profile-import-list">
                    ${analysis.warnings.map((item) => `<li>${escapeHtml(item)}</li>`).join('')}
                </ul>
            ` : ''}
            ${analysis.notes.length ? `
                <div class="profile-import-note-block">
                    ${analysis.notes.map((item) => `<p>${escapeHtml(item)}</p>`).join('')}
                </div>
            ` : ''}
        </div>
    `;
}

function buildImportPreview(envelope, context) {
    const profiles = envelope.profiles;
    const categorySummary = buildCategorySummary(profiles);
    const analyses = profiles.map((profile) => buildCompatibilityMessages(profile, context, envelope.meta));
    const riskCount = analyses.filter((item) => item.tone === 'danger').length;
    const warnCount = analyses.filter((item) => item.tone === 'warning').length;
    const summaryLines = [
        `Профилей к импорту: ${profiles.length}`,
        categorySummary ? `Состав: ${categorySummary}` : '',
        envelope.schema ? `Schema: ${envelope.schema}` : 'Schema: legacy/plain JSON',
        envelope.meta?.scope ? `Scope: ${envelope.meta.scope}` : '',
        envelope.meta?.firmwareVersion ? `Экспорт с прошивки: ${envelope.meta.firmwareVersion}` : '',
        envelope.meta?.board ? `Плата: ${envelope.meta.board}` : '',
        envelope.meta?.exportedAt ? `Дата экспорта: ${formatDateTime(envelope.meta.exportedAt)}` : '',
        context?.heaterPowerW ? `Текущая установка: ${formatNumber(context.heaterPowerW, 0, ' Вт')} • ${formatNumber(context.columnHeightMm, 0, ' мм')} • куб ${formatNumber(context.cubeVolumeL, 1, ' л')}` : '',
        riskCount > 0 ? `Профилей с риском несовместимости: ${riskCount}` : '',
        warnCount > 0 ? `Профилей, которые стоит перепроверить: ${warnCount}` : ''
    ].filter(Boolean).join('\n');

    const detailsHtml = `
        <div class="profile-import-meta">
            <div class="profile-import-meta-title">Проверка совместимости перед импортом</div>
            <div class="profile-import-meta-copy">
                Сравнение выполнено по validation context профиля, мощности, высоте колонны, насадке, объёму куба и текущим safety-лимитам.
            </div>
        </div>
        <div class="profile-import-cards">
            ${analyses.map(renderCompatibilityCard).join('')}
        </div>
    `;

    return {
        profiles,
        summary: summaryLines,
        detailsHtml
    };
}

function updateImportPreview(summaryText = '', detailsHtml = '', visible = false) {
    const preview = document.getElementById('import-preview');
    const previewText = document.getElementById('import-preview-text');
    const previewDetails = document.getElementById('import-preview-details');
    if (!preview || !previewText || !previewDetails) {
        return;
    }

    previewText.textContent = summaryText;
    previewDetails.innerHTML = detailsHtml;
    preview.style.display = visible ? 'block' : 'none';
}

async function fetchExportContext() {
    const [versionMeta, deviceContext] = await Promise.all([
        fetchJsonOrEmpty('/api/version').then((payload) => ({
            firmwareVersion: payload?.firmware || payload?.version || '',
            board: payload?.board || payload?.chipModel || ''
        })),
        fetchImportContext()
    ]);

    return { versionMeta, deviceContext };
}

// Экспорт одного профиля
export async function exportProfile(id) {
    try {
        const [profileResponse, exportContext] = await Promise.all([
            fetch(`/api/profiles/${id}/export`),
            fetchExportContext()
        ]);

        if (!profileResponse.ok) {
            throw new Error('Не удалось экспортировать профиль');
        }

        const profile = await profileResponse.json();
        const fileName = `profile_${safeFileNamePart(profile?.metadata?.name, id)}_${id}.json`;

        downloadJsonFile({
            schema: PROFILES_SNAPSHOT_SCHEMA,
            exportedAt: new Date().toISOString(),
            meta: buildSnapshotMeta(exportContext.versionMeta, exportContext.deviceContext, {
                scope: 'single-profile',
                profileCount: 1
            }),
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
        const [profilesResponse, exportContext] = await Promise.all([
            fetch(`/api/profiles/export${includeBuiltin ? '?includeBuiltin=true' : ''}`),
            fetchExportContext()
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
            meta: buildSnapshotMeta(exportContext.versionMeta, exportContext.deviceContext, {
                scope: 'profiles-batch',
                includeBuiltin,
                profileCount: profiles.length
            }),
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
    importContextPromise = fetchImportContext();
    document.getElementById('import-file-input').value = '';
    document.getElementById('import-btn').disabled = true;
    updateImportPreview('', '', false);
    document.getElementById('profile-import-modal').style.display = 'flex';

    document.getElementById('import-file-input').onchange = function (event) {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = function (loadEvent) {
            Promise.resolve(importContextPromise)
                .then((context) => {
                    const parsed = JSON.parse(loadEvent.target.result);
                    const envelope = normalizeImportEnvelope(parsed);
                    const preview = buildImportPreview(envelope, context || {});
                    importPayload = preview.profiles;
                    updateImportPreview(preview.summary, preview.detailsHtml, true);
                    document.getElementById('import-btn').disabled = preview.profiles.length === 0;
                })
                .catch((error) => {
                    console.error('Ошибка чтения файла профилей:', error);
                    importPayload = null;
                    updateImportPreview('', '', false);
                    document.getElementById('import-btn').disabled = true;
                    alert(`❌ Ошибка чтения файла: ${error.message || 'Неверный формат JSON'}`);
                });
        };
        reader.readAsText(file);
    };
}

// Закрыть модальное окно импорта
export function closeImportModal() {
    document.getElementById('profile-import-modal').style.display = 'none';
    importPayload = null;
    importContextPromise = null;
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
