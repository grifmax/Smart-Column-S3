import { closeTopMenu } from './top-menu.js';

// ============================================================================
// Tabs - syncs sidebar + dropdown active states
// ============================================================================

const tabTitles = {
    monitor: 'Главная',
    control: 'Режимы',
    profiles: 'Профили',
    settings: 'Настройки',
    wifi: 'WiFi',
    equipment: 'Оборудование',
    safety: 'Безопасность',
    history: 'История',
    tools: 'Инструменты'
};

function updateToolbarTitle(targetId) {
    const titleEl = document.getElementById('toolbar-page-title');
    if (!titleEl) return;

    const compactMonitor = targetId === 'monitor' && window.matchMedia('(max-width: 900px)').matches;
    titleEl.textContent = compactMonitor ? '' : (tabTitles[targetId] || '');
}

export function initTabs() {
    const tabs = document.querySelectorAll('.tab');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const targetId = tab.getAttribute('data-tab');

            if (!targetId) {
                closeTopMenu();
                return;
            }

            tabs.forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            document.querySelectorAll('.sidebar-item').forEach(s => {
                if (!s.getAttribute('data-tab')) return;
                s.classList.remove('active');
            });

            document.querySelectorAll(`.tab[data-tab="${targetId}"]`).forEach(t => t.classList.add('active'));

            const targetEl = document.getElementById(targetId);
            if (targetEl) {
                targetEl.classList.add('active');
            }

            updateToolbarTitle(targetId);

            if (targetId === 'history' && typeof loadHistoryList === 'function') {
                loadHistoryList();
            }

            closeTopMenu();
        });
    });
}

export function activateTabById(targetId) {
    const tab = document.querySelector(`.tab[data-tab="${targetId}"]`);
    if (tab) tab.click();
}

window.addEventListener('resize', () => {
    const activeTab = document.querySelector('.tab.active[data-tab]');
    if (activeTab) {
        updateToolbarTitle(activeTab.getAttribute('data-tab'));
    }
});
