export const OPERATOR_VIEW_STORAGE_KEY = 'ui.operatorView';
const OPERATOR_COMPACT_BREAKPOINT = 1024;

let autoSyncBound = false;
let autoSyncTimer = null;

function getResponsiveViewportWidth() {
    const widths = [];

    const visual = Number(window.visualViewport?.width);
    if (Number.isFinite(visual) && visual > 0) widths.push(visual);

    const inner = Number(window.innerWidth);
    if (Number.isFinite(inner) && inner > 0) widths.push(inner);

    const client = Number(document.documentElement?.clientWidth);
    if (Number.isFinite(client) && client > 0) widths.push(client);

    const monitorWidth = Number(document.querySelector('#monitor .operator-screen')?.getBoundingClientRect?.().width);
    if (Number.isFinite(monitorWidth) && monitorWidth > 0) widths.push(monitorWidth);

    if (!widths.length) return 1920;
    return Math.min(...widths);
}

function scheduleAutoSync() {
    clearTimeout(autoSyncTimer);
    autoSyncTimer = setTimeout(() => {
        syncOperatorViewAuto();
    }, 120);
}

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

// Auto mode: instrument on wide viewports, compact on narrower screens.
export function getAutoOperatorView() {
    return getResponsiveViewportWidth() >= OPERATOR_COMPACT_BREAKPOINT ? 'instrument' : 'compact';
}

export function syncOperatorViewAuto() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;
    setOperatorView(getAutoOperatorView());
}

export function initOperatorViewToggle() {
    const screen = document.querySelector('#monitor .operator-screen');
    if (!screen) return;
    const button = document.getElementById('operator-view-toggle');

    if (!button) {
        setOperatorView('instrument');
        return;
    }

    // Initial auto-select by effective viewport width.
    syncOperatorViewAuto();

    if (autoSyncBound) return;
    autoSyncBound = true;

    // React to viewport and layout changes (window resize, zoom, orientation).
    window.addEventListener('resize', scheduleAutoSync);
    window.addEventListener('orientationchange', scheduleAutoSync);

    if (window.visualViewport) {
        window.visualViewport.addEventListener('resize', scheduleAutoSync);
        window.visualViewport.addEventListener('scroll', scheduleAutoSync);
    }

    if (typeof ResizeObserver !== 'undefined') {
        const observer = new ResizeObserver(() => scheduleAutoSync());
        observer.observe(document.documentElement);
    }
}
