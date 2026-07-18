// ============================================================================
// Утилиты
// ============================================================================

export function pad(num, size = 2) {
    let s = String(num);
    while (s.length < size) s = '0' + s;
    return s;
}

export function formatUptime(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0) return '0:00:00';

    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);

    return `${h}:${pad(m)}:${pad(s)}`;
}

const PROCESS_PHASE_LABELS = Object.freeze({
    idle: 'Ожидание',
    prepare: 'Подготовка',
    heating: 'Нагрев',
    stabilization: 'Стабилизация',
    heads: 'Отбор голов',
    post_heads: 'Постстабилизация',
    post_heads_stabilization: 'Постстабилизация',
    purge: 'Продувка',
    body: 'Отбор тела',
    tails: 'Отбор хвостов',
    feed_ramp: 'Запуск подачи',
    working: 'Работа',
    cooling: 'Охлаждение',
    fermentation: 'Брожение',
    acid_rest: 'Кислотная пауза',
    protein_rest: 'Белковая пауза',
    beta_amylase: 'Бета-амилазная пауза',
    alpha_amylase: 'Альфа-амилазная пауза',
    mash_out: 'Мэш-аут',
    finish: 'Завершение',
    completed: 'Завершено',
    error: 'Ошибка',
    unknown: 'Неизвестная фаза'
});

export function formatProcessPhase(phase, fallback = '—') {
    const key = String(phase ?? '').trim().toLowerCase();
    return PROCESS_PHASE_LABELS[key] || fallback;
}
