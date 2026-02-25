// ============================================================================

// Tabs

// ============================================================================



export function initTabs() {

    // В навигации есть и кнопки-вкладки (data-tab), и ссылки на другие страницы (<a>).
    // Для вкладок работаем только с элементами, у которых есть data-tab.
    const tabs = document.querySelectorAll('.tab');

    tabs.forEach(tab => {

        tab.addEventListener('click', () => {

            const targetId = tab.getAttribute('data-tab');

            // Это ссылка на другую страницу (charts/manual/...), не трогаем.
            if (!targetId) {
                closeTopMenu();
                return;
            }



            // Убрать активный класс со всех вкладок

            tabs.forEach(t => t.classList.remove('active'));

            document.querySelectorAll('.tab-content').forEach(content => {

                content.classList.remove('active');

            });



            // Добавить активный класс к выбранной вкладке

            tab.classList.add('active');

            const targetEl = document.getElementById(targetId);
            if (targetEl) {
                targetEl.classList.add('active');
            }



            // Загрузить историю при переключении на вкладку "История"

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
