import { closeTopMenu } from './top-menu.js';

// ============================================================================
// Tabs — syncs sidebar + dropdown active states
// ============================================================================

const tabTitles = {
    monitor: 'Главная',
    control: 'Режимы',
    settings: 'Настройки',
    wifi: 'WiFi',
    equipment: 'Оборудование',
    safety: '\u0411\u0435\u0437\u043e\u043f\u0430\u0441\u043d\u043e\u0441\u0442\u044c',
    history: 'История',
    tools: 'Инструменты'
};

export function initTabs() {
    const tabs = document.querySelectorAll('.tab');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const targetId = tab.getAttribute('data-tab');

            // External link (charts/logs), just close menu
            if (!targetId) {
                closeTopMenu();
                return;
            }

            // Deactivate all tabs (both sidebar and dropdown)
            tabs.forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            // Also deactivate sidebar-item links (non-tab items like charts/logs)
            document.querySelectorAll('.sidebar-item').forEach(s => {
                if (!s.getAttribute('data-tab')) return;
                s.classList.remove('active');
            });

            // Activate ALL matching tabs (sidebar + dropdown may both have same data-tab)
            document.querySelectorAll(`.tab[data-tab="${targetId}"]`).forEach(t => t.classList.add('active'));

            const targetEl = document.getElementById(targetId);
            if (targetEl) {
                targetEl.classList.add('active');
            }

            // Update toolbar page title
            const titleEl = document.getElementById('toolbar-page-title');
            if (titleEl && tabTitles[targetId]) {
                titleEl.textContent = tabTitles[targetId];
            }

            // Load history on tab switch
            if (targetId === 'history') {
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

