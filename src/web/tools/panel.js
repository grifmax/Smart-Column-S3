const DESKTOP_MEDIA = '(max-width: 900px)';
const ACTIVE_TOOL_KEY = 'tools-workbench-active-tool';

const TOOL_GROUPS = [
    {
        id: 'spirit',
        label: 'Спирт и продукт',
        tools: ['yield', 'reverse', 'blend', 'abv', 'dilution'],
    },
    {
        id: 'fermentation',
        label: 'Брожение и сусло',
        tools: ['potential', 'density', 'fermentation'],
    },
    {
        id: 'process',
        label: 'Энергия и колонна',
        tools: ['heat', 'selection'],
    },
];

const TOOL_DEFS = [
    {
        id: 'yield',
        selector: '#calc-yield-volume-l',
        group: 'spirit',
        icon: '🥃',
        title: 'AA / Фракции / Время',
        shortTitle: 'AA и фракции',
        description: 'Абсолютный спирт, фракции и ориентировочное время отбора.',
    },
    {
        id: 'reverse',
        selector: '#calc-reverse-target-volume',
        group: 'spirit',
        icon: '📦',
        title: 'Обратный расчет партии',
        shortTitle: 'Обратный расчет',
        description: 'Сколько нужно сырца или нейтрали под заданный объем продукта.',
    },
    {
        id: 'blend',
        selector: '#calc-blend-target-abv',
        group: 'spirit',
        icon: '🥂',
        title: 'Купаж фракций',
        shortTitle: 'Купаж',
        description: 'Сводит несколько фракций в одну смесь и считает итоговую крепость.',
    },
    {
        id: 'abv',
        selector: '#calc-abv-raw',
        group: 'spirit',
        icon: '🌡️',
        title: 'Коррекция спиртометра',
        shortTitle: 'Спиртометр',
        description: 'Поправка спиртометра на температуру с приведением к 20°C.',
    },
    {
        id: 'dilution',
        selector: '#calc-dil-volume',
        group: 'spirit',
        icon: '💧',
        title: 'Разбавление по этапам',
        shortTitle: 'Разбавление',
        description: 'Пошаговое разведение с расчетом воды и итогового объема.',
    },
    {
        id: 'potential',
        selector: '#calc-potential-source-type',
        group: 'fermentation',
        icon: '🍯',
        title: 'Сахар / Сусло → спирт',
        shortTitle: 'Сахар -> спирт',
        description: 'Потенциал браги, абсолютный спирт и ориентиры по сырью.',
    },
    {
        id: 'density',
        selector: '#calc-density-scale',
        group: 'fermentation',
        icon: '📏',
        title: 'Конвертер плотности',
        shortTitle: 'Плотность',
        description: 'Brix, Plato, SG и Oechsle в одном месте.',
    },
    {
        id: 'fermentation',
        selector: '#calc-ferment-basis',
        group: 'fermentation',
        icon: '🫙',
        title: 'Калькулятор брожения',
        shortTitle: 'Брожение',
        description: 'OG/FG, дозировки дрожжей, срок брожения и гидромодуль.',
    },
    {
        id: 'heat',
        selector: '#calc-heat-volume',
        group: 'process',
        icon: '⚡',
        title: 'Нагрев / энергия / стоимость',
        shortTitle: 'Нагрев',
        description: 'Время нагрева, расход энергии и стоимость по тарифу.',
    },
    {
        id: 'selection',
        selector: '#calc-select-diameter',
        group: 'process',
        icon: '🧪',
        title: 'Режим отбора',
        shortTitle: 'Отбор',
        description: 'Грубая оценка мощности и рабочего диапазона колонны.',
    },
];

function getGroupLabel(groupId) {
    return TOOL_GROUPS.find((group) => group.id === groupId)?.label ?? 'Инструменты';
}

function createElement(tag, className, textContent) {
    const element = document.createElement(tag);
    if (className) {
        element.className = className;
    }
    if (typeof textContent === 'string') {
        element.textContent = textContent;
    }
    return element;
}

function buildMobileToggle(meta) {
    const button = createElement('button', 'tools-card-mobile-toggle');
    button.type = 'button';

    const icon = createElement('span', 'tools-card-mobile-icon', meta.icon);
    icon.setAttribute('aria-hidden', 'true');

    const copy = createElement('span', 'tools-card-mobile-copy');
    copy.append(
        createElement('span', 'tools-card-mobile-title', meta.shortTitle),
        createElement('span', 'tools-card-mobile-description', meta.description)
    );

    const chevron = createElement('span', 'tools-card-mobile-chevron', '▾');
    chevron.setAttribute('aria-hidden', 'true');

    button.append(icon, copy, chevron);
    return button;
}

function buildDesktopNav(tools) {
    const nav = createElement('aside', 'tools-sidebar-nav workbench-local-nav');
    nav.setAttribute('aria-label', 'Навигация по калькуляторам');

    const navHeader = createElement('div', 'tools-sidebar-header');
    navHeader.append(
        createElement('div', 'tools-sidebar-title', 'Инструменты'),
        createElement('div', 'tools-sidebar-subtitle', 'Открыт один рабочий калькулятор, остальные доступны через меню.')
    );
    nav.appendChild(navHeader);

    const buttonsById = new Map();

    for (const group of TOOL_GROUPS) {
        const groupTools = group.tools
            .map((toolId) => tools.find((tool) => tool.meta.id === toolId))
            .filter(Boolean);

        if (!groupTools.length) {
            continue;
        }

        nav.appendChild(createElement('div', 'sidebar-section-title', group.label));

        for (const tool of groupTools) {
            const button = createElement('button', 'equipment-local-nav-btn workbench-local-nav-btn tools-nav-item');
            button.type = 'button';
            button.dataset.toolId = tool.meta.id;
            button.append(
                createElement('span', 'icon', tool.meta.icon),
                createElement('span', 'label', tool.meta.shortTitle)
            );
            nav.appendChild(button);
            buttonsById.set(tool.meta.id, button);
        }
    }

    return { nav, buttonsById };
}

function enhanceCard(card, meta) {
    if (card.dataset.toolsEnhanced === '1') {
        return {
            card,
            meta,
            body: card.querySelector('.tools-card-body'),
            toggle: card.querySelector('.tools-card-mobile-toggle'),
        };
    }

    card.dataset.toolsEnhanced = '1';
    card.dataset.toolId = meta.id;
    card.dataset.toolGroup = meta.group;

    const children = [...card.childNodes];
    const body = createElement('div', 'tools-card-body');
    for (const child of children) {
        body.appendChild(child);
    }

    const titleEl = body.querySelector('h2');
    if (titleEl) {
        titleEl.classList.add('tools-card-title');
        titleEl.textContent = '';

        const titleIcon = createElement('span', 'tools-card-title-icon', meta.icon);
        titleIcon.setAttribute('aria-hidden', 'true');
        const titleText = createElement('span', 'tools-card-title-text', meta.title);
        titleEl.append(titleIcon, titleText);

        const groupBadge = createElement('div', 'tools-card-group-badge', getGroupLabel(meta.group));
        titleEl.before(groupBadge);
    }

    const descriptionEl = body.querySelector('.tools-card-text');
    if (descriptionEl) {
        descriptionEl.textContent = meta.description;
    }

    const toggle = buildMobileToggle(meta);

    card.textContent = '';
    card.append(toggle, body);

    return { card, meta, body, toggle };
}

function resolveTools(cards) {
    const resolved = [];
    const missing = [];

    for (const meta of TOOL_DEFS) {
        const card = cards.find((candidate) => candidate.querySelector(meta.selector));
        if (!card) {
            missing.push(meta.id);
            continue;
        }

        resolved.push(enhanceCard(card, meta));
    }

    if (missing.length > 0) {
        console.warn('Tools workbench: some calculators were not found', missing);
    }

    return resolved;
}

export function initToolsWorkbench() {
    const toolsRoot = document.getElementById('tools');
    const cardsContainer = toolsRoot?.querySelector('.tools-cards');
    if (!toolsRoot || !cardsContainer || cardsContainer.dataset.toolsWorkbench === '1') {
        return;
    }

    const cards = [...cardsContainer.querySelectorAll('.tools-card')];
    const tools = resolveTools(cards);
    if (!tools.length) {
        return;
    }

    cardsContainer.dataset.toolsWorkbench = '1';

    const shell = createElement('div', 'tools-shell workbench-shell');
    const main = createElement('div', 'tools-main');
    const { nav, buttonsById } = buildDesktopNav(tools);

    cardsContainer.parentNode.insertBefore(shell, cardsContainer);
    shell.append(nav, main);
    main.appendChild(cardsContainer);

    const mediaQuery = window.matchMedia(DESKTOP_MEDIA);
    const preferredToolId = localStorage.getItem(ACTIVE_TOOL_KEY);
    let lastSelectedToolId = tools.some((tool) => tool.meta.id === preferredToolId)
        ? preferredToolId
        : tools[0].meta.id;
    let activeToolId = lastSelectedToolId;

    function scrollToolToggleIntoView(toolId) {
        const tool = tools.find((entry) => entry.meta.id === toolId);
        if (!tool) {
            return;
        }

        requestAnimationFrame(() => {
            tool.toggle.scrollIntoView({
                block: 'start',
                behavior: 'smooth',
            });
        });
    }

    function syncUi() {
        const isMobile = mediaQuery.matches;
        if (!isMobile && !activeToolId) {
            activeToolId = lastSelectedToolId || tools[0].meta.id;
        }
        if (isMobile && !activeToolId) {
            activeToolId = lastSelectedToolId || tools[0].meta.id;
        }

        toolsRoot.dataset.toolsLayout = isMobile ? 'mobile' : 'desktop';
        nav.hidden = false;

        for (const tool of tools) {
            const isActive = tool.meta.id === activeToolId;
            tool.card.classList.toggle('is-active', isActive);
            if (tool.toggle) {
                tool.toggle.classList.toggle('is-active', isActive);
                tool.toggle.setAttribute('aria-expanded', String(isActive));
                tool.toggle.hidden = isMobile;
            }
            tool.body.hidden = false;
            tool.card.hidden = !isActive;

            const navButton = buttonsById.get(tool.meta.id);
            if (navButton) {
                navButton.classList.toggle('active', isActive);
                navButton.setAttribute('aria-current', isActive ? 'true' : 'false');
            }
        }
    }

    function setActiveTool(toolId) {
        if (toolId !== null && !tools.some((tool) => tool.meta.id === toolId)) {
            return;
        }

        activeToolId = toolId;
        if (toolId) {
            lastSelectedToolId = toolId;
            localStorage.setItem(ACTIVE_TOOL_KEY, toolId);
        }

        syncUi();
    }

    for (const tool of tools) {
        tool.toggle.addEventListener('click', () => {
            const isMobile = mediaQuery.matches;
            const isSameTool = activeToolId === tool.meta.id;

            if (isMobile && isSameTool) {
                setActiveTool(null);
                return;
            }

            setActiveTool(tool.meta.id);
            if (isMobile) {
                scrollToolToggleIntoView(tool.meta.id);
            }
        });
    }

    for (const [toolId, button] of buttonsById.entries()) {
        button.addEventListener('click', () => {
            setActiveTool(toolId);
            main.scrollIntoView({ block: 'start', behavior: 'smooth' });
        });
    }

    if (typeof mediaQuery.addEventListener === 'function') {
        mediaQuery.addEventListener('change', syncUi);
    } else if (typeof mediaQuery.addListener === 'function') {
        mediaQuery.addListener(syncUi);
    }

    syncUi();
}
