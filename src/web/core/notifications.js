import { addLog } from './logs.js';

export let notificationsEnabled = false;

function isNotificationContextAllowed() {
    if (window.isSecureContext) return true;
    const host = window.location?.hostname || '';
    return host === 'localhost' || host === '127.0.0.1' || host === '::1';
}

function getNotificationAvailability() {
    if (!('Notification' in window)) {
        return {
            available: false,
            reason: 'Браузер не поддерживает уведомления'
        };
    }

    if (!isNotificationContextAllowed()) {
        return {
            available: false,
            reason: 'Для браузерных уведомлений откройте Web UI по HTTPS (или localhost)'
        };
    }

    return {
        available: true,
        reason: ''
    };
}

export async function initNotifications() {
    const checkbox = document.getElementById('browser-notifications-enabled');
    const availability = getNotificationAvailability();

    if (!availability.available) {
        notificationsEnabled = false;
        if (checkbox) {
            checkbox.checked = false;
            checkbox.disabled = false;
            checkbox.title = availability.reason;
        }
        return false;
    }

    const saved = localStorage.getItem('browser-notifications') === '1';
    notificationsEnabled = Notification.permission === 'granted' && saved;

    if (checkbox) {
        checkbox.checked = notificationsEnabled;
        checkbox.disabled = false;
        checkbox.title = '';
    }

    return notificationsEnabled;
}

export async function requestNotificationPermission() {
    const availability = getNotificationAvailability();
    if (!availability.available) {
        alert(availability.reason);
        notificationsEnabled = false;
        return false;
    }

    if (Notification.permission === 'denied') {
        notificationsEnabled = false;
        alert('Разрешение на уведомления заблокировано в браузере. Разрешите их в настройках сайта.');
        return false;
    }

    const permission = await Notification.requestPermission();
    notificationsEnabled = permission === 'granted';
    return notificationsEnabled;
}

export function showNotification(title, options = {}) {
    if (!notificationsEnabled) return;
    if (Notification.permission !== 'granted') return;
    if (!getNotificationAvailability().available) return;

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
            try {
                new Notification(title, defaultOptions);
            } catch {
                // no-op
            }
        });
    } else {
        try {
            new Notification(title, defaultOptions);
        } catch {
            // no-op
        }
    }
}

export async function toggleBrowserNotifications() {
    const checkbox = document.getElementById('browser-notifications-enabled');
    if (!checkbox) return;

    const availability = getNotificationAvailability();
    if (!availability.available) {
        notificationsEnabled = false;
        checkbox.checked = false;
        checkbox.disabled = false;
        checkbox.title = availability.reason;
        localStorage.setItem('browser-notifications', '0');
        addLog(availability.reason, 'warning');
        alert(availability.reason);
        return;
    }

    checkbox.disabled = false;
    checkbox.title = '';

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
    const availability = getNotificationAvailability();
    if (!availability.available) {
        alert(availability.reason);
        return;
    }

    if (!notificationsEnabled) {
        alert('Сначала включите уведомления!');
        return;
    }

    showNotification('Тестовое уведомление', {
        body: 'Smart-Column S3: уведомления работают корректно!'
    });
}
