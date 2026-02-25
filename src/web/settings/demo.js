export function toggleDemoMode() {

    const enabled = document.getElementById('demo-mode-enabled').checked;



    // Сохранить в localStorage

    localStorage.setItem('demoMode', enabled ? 'true' : 'false');



    // Отправить на сервер

    fetch('/api/settings/demo', {

        method: 'POST',

        headers: { 'Content-Type': 'application/json' },

        body: JSON.stringify({ enabled: enabled })

    }).then(response => {

        if (response.ok) {

            addLog(enabled ? '🧪 Демо-режим ВКЛЮЧЁН' : '✅ Демо-режим отключён', 'info');

        } else {

            addLog('⚠️ Ошибка сохранения демо-режима на сервер', 'warning');

        }

    }).catch(err => {

        addLog('⚠️ Демо-режим сохранён локально (сервер недоступен)', 'warning');

    });

}



export function loadDemoMode() {

    const saved = localStorage.getItem('demoMode');

    const checkbox = document.getElementById('demo-mode-enabled');

    if (checkbox && saved === 'true') {

        checkbox.checked = true;

    }

}



// Перезагрузка контроллера

export function rebootController() {

    if (!confirm('Перезагрузить контроллер ESP32?\n\nВсе текущие процессы будут остановлены!')) {

        return;

    }



    addLog('🔄 Отправка команды перезагрузки...', 'warning');



    fetch('/api/reboot', {

        method: 'POST'

    }).then(response => {

        if (response.ok) {

            addLog('✓ Контроллер перезагружается...', 'success');

            // Показать сообщение и попробовать переподключиться через 5 сек

            setTimeout(() => {

                addLog('📌 Попытка переподключения...', 'info');

                window.location.reload();

            }, 5000);

        } else {

            addLog('✗ Ошибка перезагрузки', 'error');

        }

    }).catch(err => {

        addLog('❌ Ошибка сети: ' + err.message, 'error');

    });

}
