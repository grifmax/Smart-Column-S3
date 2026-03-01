// ============================================================================



// Закрытие модального окна сравнения при клике на overlay

document.addEventListener('DOMContentLoaded', function () {

    const compareOverlay = document.getElementById('compare-modal');

    if (compareOverlay) {

        compareOverlay.addEventListener('click', function (e) {

            if (e.target === compareOverlay) {

                closeCompareModal();

            }

        });

    }

});

// ============================================================================

// Информация о пользователе

// ============================================================================



export async function loadUserInfo() {

    try {

        const response = await fetch('/api/web/user', {

            credentials: 'same-origin' // Важно для отправки cookies/сессий

        });



        if (!response.ok) {

            const errorText = await response.text();

            console.error('Failed to load user info:', response.status, errorText);



            if (response.status === 401) {

                // Пользователь не авторизован - редирект на логин

                const usernameElement = document.getElementById('current-username');

                if (usernameElement) {

                    usernameElement.textContent = 'Не авторизован';

                }

                // Не редиректим автоматически, чтобы избежать зацикливания

                // Вместо этого показываем сообщение

                return;

            }

            throw new Error('Failed to load user info: ' + response.status);

        }



        const user = await response.json();

        const usernameElement = document.getElementById('current-username');

        if (usernameElement) {

            usernameElement.textContent = user.username || 'Неизвестно';

        }

    } catch (error) {

        console.error('Error loading user info:', error);

        const usernameElement = document.getElementById('current-username');

        if (usernameElement) {

            usernameElement.textContent = 'Ошибка загрузки';

        }

    }

}



// Показать/скрыть меню пользователя

export function toggleUserMenu() {

    const menu = document.getElementById('user-menu');

    if (menu) {

        menu.style.display = menu.style.display === 'none' ? 'block' : 'none';

    }

}



// Закрыть меню при клике вне его

document.addEventListener('click', function (event) {

    const userInfo = document.getElementById('user-info');

    const menu = document.getElementById('user-menu');

    if (menu && userInfo && !userInfo.contains(event.target) && !menu.contains(event.target)) {

        menu.style.display = 'none';

    }

});



// Выход из системы

export async function logout() {

    try {

        const response = await fetch('/api/web/user/logout');

        if (response.ok) {

            window.location.href = '/login';

        } else {

            alert('Ошибка выхода из системы');

        }

    } catch (error) {

        console.error('Error logging out:', error);

        alert('Ошибка выхода из системы');

    }

}



// Сменить аккаунт

export async function switchAccount() {

    // Важно: просто перейти на /login недостаточно — login.php при активной сессии
    // сразу редиректит обратно на главную. Поэтому сначала выходим из текущего аккаунта.
    try {
        await fetch('/api/web/user/logout', { credentials: 'same-origin' });
    } catch (error) {
        console.warn('Switch account: logout request failed, redirecting to login anyway:', error);
    }

    window.location.href = '/login?switch=1';

}
