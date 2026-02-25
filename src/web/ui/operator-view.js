export const OPERATOR_VIEW_STORAGE_KEY = 'ui.operatorView';

export function setOperatorView(view) {
    const screen = document.querySelector('#monitor .operator-screen');
    const button = document.getElementById('operator-view-toggle');
    if (!screen) return;

    const normalizedView = view === 'compact' ? 'compact' : 'instrument';
    const isInstrument = normalizedView === 'instrument';
    screen.setAttribute('data-view', normalizedView);
    screen.classList.toggle('operator-screen-instrument', isInstrument);
    screen.classList.toggle('operator-screen-compact', !isInstrument);

    if (button) {
        button.textContent = `View: ${isInstrument ? 'Instrument' : 'Compact'}`;
        button.classList.toggle('btn-info', isInstrument);
        button.classList.toggle('btn-warning', !isInstrument);
    }
}

export function toggleOperatorView() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;

    const nextView = screen.classList.contains('operator-screen-instrument') ? 'compact' : 'instrument';
    setOperatorView(nextView);
    try {
        localStorage.setItem(OPERATOR_VIEW_STORAGE_KEY, nextView);
    } catch (e) {
        console.warn('operator view save failed:', e);
    }
}

// Авто-выбор: Instrument ≥768px, Compact <768px
export function getAutoOperatorView() {
    return window.innerWidth >= 768 ? 'instrument' : 'compact';
}

export function initOperatorViewToggle() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;
    const button = document.getElementById('operator-view-toggle');

    if (!button) {
        setOperatorView('instrument');
        return;
    }

    // При первом запуске — авто-выбор по ширине экрана
    setOperatorView(getAutoOperatorView());

    // Авто-переключение при resize (без ручного override)
    let resizeTimer;
    window.addEventListener('resize', function () {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(function () {
            setOperatorView(getAutoOperatorView());
        }, 200);
    });
}
