import { addLog } from './logs.js';

export let notificationsEnabled = false;

export async function initNotifications() {
    const checkbox = document.getElementById('browser-notifications-enabled');

    if (!('Notification' in window)) {
        if (checkbox) {
            checkbox.checked = false;
            checkbox.disabled = true;
        }
        return false;
    }

    const saved = localStorage.getItem('browser-notifications') === '1';
    notificationsEnabled = Notification.permission === 'granted' && saved;

    if (checkbox) {
        checkbox.checked = notificationsEnabled;
    }

    return notificationsEnabled;
}

export async function requestNotificationPermission() {
    if (!('Notification' in window)) {
        alert('Ваш браузер не поддерживает Push-уведомления');
        return false;
    }
    const permission = await Notification.requestPermission();
    notificationsEnabled = permission === 'granted';
    return notificationsEnabled;
}

export function showNotification(title, options = {}) {
    if (!notificationsEnabled) return;

    const defaultOptions = {
        icon: '/manifest/icon-192.png',
        badge: '/manifest/icon-72.png',
        vibrate: [200, 100, 200],
        ...options
    };

    if (navigator.serviceWorker && navigator.serviceWorker.ready) {
        navigator.serviceWorker.ready.then((registration) => {
            registration.showNotification(title, defaultOptions);
        }).catch(() => {
            try { new Notification(title, defaultOptions); } catch { }
        });
    } else {
        try { new Notification(title, defaultOptions); } catch { }
    }
}

export async function toggleBrowserNotifications() {
    const checkbox = document.getElementById('browser-notifications-enabled');
    if (!checkbox) return;

    if (!('Notification' in window)) {
        checkbox.checked = false;
        addLog('Браузер не поддерживает уведомления', 'warning');
        return;
    }

    if (checkbox.checked) {
        const granted = await requestNotificationPermission();
        checkbox.checked = granted;
        if (granted) {
            addLog('✓ Браузерные уведомления включены', 'success');
        } else {
            addLog('✗ Нет разрешения на браузерные уведомления', 'error');
        }
    } else {
        notificationsEnabled = false;
        addLog('ℹ Браузерные уведомления отключены', 'info');
    }

    localStorage.setItem('browser-notifications', notificationsEnabled ? '1' : '0');
}

export function testBrowserNotification() {
    if (!notificationsEnabled) {
        alert('Сначала включите уведомления!');
        return;
    }
    showNotification('Тестовое уведомление', {
        body: 'Smart-Column S3: уведомления работают корректно!'
    });
}
