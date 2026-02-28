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

    const isMobile = window.matchMedia('(max-width: 768px)').matches;
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

    window.addEventListener('resize', () => {
        const dropdown = document.getElementById('top-menu-dropdown');
        if (dropdown && dropdown.classList.contains('open')) {
            positionTopMenuDropdown();
        }
    });
}
