function getResponsiveViewportWidth() {
    const widths = [];

    const visual = Number(window.visualViewport?.width);
    if (Number.isFinite(visual) && visual > 0) widths.push(visual);

    const inner = Number(window.innerWidth);
    if (Number.isFinite(inner) && inner > 0) widths.push(inner);

    const client = Number(document.documentElement?.clientWidth);
    if (Number.isFinite(client) && client > 0) widths.push(client);

    if (!widths.length) return 1920;
    return Math.min(...widths);
}

function buildDropdownSectionTitle(source) {
    const title = document.createElement('div');
    title.className = 'sidebar-section-title menu-dropdown-section-title';
    title.textContent = source.textContent?.trim() || '';
    return title;
}

function buildDropdownMenuItem(source) {
    const item = source.cloneNode(true);
    item.classList.add('menu-dropdown-item');
    item.classList.remove('tab');
    item.removeAttribute('id');

    if (item instanceof HTMLButtonElement) {
        item.type = 'button';
    }

    const tabId = source.getAttribute('data-tab');
    if (tabId) {
        item.dataset.menuTab = tabId;
        item.removeAttribute('data-tab');
        item.removeAttribute('onclick');
    } else if (source.classList.contains('active')) {
        item.dataset.menuCurrentPage = '1';
    }

    return item;
}

function syncTopMenuFromSidebar() {
    const sidebar = document.getElementById('main-sidebar');
    const dropdown = document.getElementById('top-menu-dropdown');
    const nav = dropdown?.querySelector('.tabs-dropdown');
    if (!sidebar || !dropdown || !nav) return;

    nav.classList.add('menu-dropdown-nav');

    const fragment = document.createDocumentFragment();
    for (const node of sidebar.children) {
        if (!(node instanceof HTMLElement)) continue;
        if (node.classList.contains('sidebar-collapse-btn') ||
            node.classList.contains('sidebar-spacer') ||
            node.classList.contains('sidebar-footer')) {
            continue;
        }

        if (node.classList.contains('sidebar-section-title')) {
            fragment.appendChild(buildDropdownSectionTitle(node));
            continue;
        }

        if (node.classList.contains('sidebar-item')) {
            fragment.appendChild(buildDropdownMenuItem(node));
        }
    }

    nav.replaceChildren(fragment);
}

export function syncTopMenuActiveState(targetId = '') {
    const menuItems = document.querySelectorAll('#top-menu-dropdown .menu-dropdown-item');
    menuItems.forEach((item) => {
        const isTabMatch = Boolean(targetId) && item.getAttribute('data-menu-tab') === targetId;
        const sourceTab = isTabMatch
            ? document.querySelector(`.sidebar-item.tab[data-tab="${targetId}"]`)
            : null;
        const isStaticCurrentPage = item.getAttribute('data-menu-current-page') === '1';
        const isActive = isTabMatch || Boolean(sourceTab?.classList.contains('active')) || isStaticCurrentPage;

        item.classList.toggle('active', isActive);
        if (isActive) {
            item.setAttribute('aria-current', 'page');
        } else {
            item.removeAttribute('aria-current');
        }
    });
}

export function closeTopMenu() {
    const dropdown = document.getElementById('top-menu-dropdown');
    const toggle = document.getElementById('top-menu-toggle');
    if (dropdown) dropdown.classList.remove('open');
    if (toggle) toggle.setAttribute('aria-expanded', 'false');
}

export function positionTopMenuDropdown() {
    const dropdown = document.getElementById('top-menu-dropdown');
    const toggle = document.getElementById('top-menu-toggle');
    if (!dropdown || !toggle) return;

    const isMobile = getResponsiveViewportWidth() <= 900;
    if (!isMobile) {
        dropdown.style.position = '';
        dropdown.style.left = '';
        dropdown.style.right = '';
        dropdown.style.top = '';
        dropdown.style.width = '';
        dropdown.style.maxWidth = '';
        return;
    }

    const toggleRect = toggle.getBoundingClientRect();
    const margin = 10;
    const top = Math.max(margin, Math.round(toggleRect.bottom + 8));

    dropdown.style.position = 'fixed';
    dropdown.style.left = `${margin}px`;
    dropdown.style.right = `${margin}px`;
    dropdown.style.top = `${top}px`;
    dropdown.style.width = 'auto';
    dropdown.style.maxWidth = 'none';
}

export function toggleTopMenu(event) {
    if (event) event.stopPropagation();
    const dropdown = document.getElementById('top-menu-dropdown');
    const toggle = document.getElementById('top-menu-toggle');
    if (!dropdown || !toggle) return;
    const willOpen = !dropdown.classList.contains('open');
    if (willOpen) {
        positionTopMenuDropdown();
    }
    dropdown.classList.toggle('open', willOpen);
    toggle.setAttribute('aria-expanded', willOpen ? 'true' : 'false');
}

export function initTopMenu() {
    syncTopMenuFromSidebar();
    const activeSidebarTab = document.querySelector('.sidebar-item.active[data-tab]')?.getAttribute('data-tab') || '';
    syncTopMenuActiveState(activeSidebarTab);

    document.addEventListener('click', (event) => {
        const menuTabItem = event.target.closest('#top-menu-dropdown [data-menu-tab]');
        if (!menuTabItem) return;

        event.preventDefault();
        const targetId = menuTabItem.getAttribute('data-menu-tab');
        if (!targetId) {
            closeTopMenu();
            return;
        }

        const sourceTab = document.querySelector(`.sidebar-item.tab[data-tab="${targetId}"]`);
        if (sourceTab instanceof HTMLElement) {
            sourceTab.click();
        } else {
            closeTopMenu();
        }
    });

    document.addEventListener('click', (event) => {
        const menuItem = event.target.closest('#top-menu-dropdown .menu-dropdown-item');
        if (!menuItem || menuItem.hasAttribute('data-menu-tab')) return;
        closeTopMenu();
    });

    document.addEventListener('click', (event) => {
        const dropdown = document.getElementById('top-menu-dropdown');
        const toggle = document.getElementById('top-menu-toggle');
        if (!dropdown || !toggle) return;
        const target = event.target;
        if (dropdown.contains(target) || toggle.contains(target)) return;
        closeTopMenu();
    });

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') closeTopMenu();
    });

    const syncDropdownPosition = () => {
        const dropdown = document.getElementById('top-menu-dropdown');
        if (dropdown && dropdown.classList.contains('open')) {
            positionTopMenuDropdown();
        }
    };

    window.addEventListener('resize', syncDropdownPosition);
    window.addEventListener('orientationchange', syncDropdownPosition);
    if (window.visualViewport) {
        window.visualViewport.addEventListener('resize', syncDropdownPosition);
    }
}
