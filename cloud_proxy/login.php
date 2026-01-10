<?php
/**
 * Smart-Column S3 - Login/Register Page
 */

require_once __DIR__ . '/auth_web.php';

$error = '';
$success = '';

// Обработка входа
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'login') {
    // Получаем данные из POST, нормализуя их в UTF-8
    $username = isset($_POST['username']) ? trim($_POST['username']) : '';
    $password = isset($_POST['password']) ? $_POST['password'] : '';
    
    // Убеждаемся, что данные в UTF-8
    if (function_exists('mb_convert_encoding')) {
        $username = mb_convert_encoding($username, 'UTF-8', 'auto');
    }
    
    if (empty($username) || empty($password)) {
        $error = 'Заполните все поля';
    } else {
        $result = login($username, $password);
        if ($result['success']) {
            header('Location: /');
            exit;
        } else {
            $error = $result['message'];
        }
    }
}

// Обработка регистрации
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'register') {
    // Получаем данные из POST, нормализуя их в UTF-8
    $username = isset($_POST['username']) ? trim($_POST['username']) : '';
    $password = isset($_POST['password']) ? $_POST['password'] : '';
    $email = isset($_POST['email']) ? trim($_POST['email']) : '';
    
    // Убеждаемся, что данные в UTF-8
    if (function_exists('mb_convert_encoding')) {
        $username = mb_convert_encoding($username, 'UTF-8', 'auto');
        $email = mb_convert_encoding($email, 'UTF-8', 'auto');
    }
    
    if (empty($username) || empty($password)) {
        $error = 'Заполните логин и пароль';
    } else {
        $result = register($username, $password, $email);
        if ($result['success']) {
            header('Location: /');
            exit;
        } else {
            $error = $result['message'];
        }
    }
}

// Если уже авторизован - редирект
if (isAuthenticated()) {
    header('Location: /');
    exit;
}

?><!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Вход - Smart-Column S3</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            max-width: 400px;
            width: 100%;
            padding: 40px;
        }
        h1 {
            color: #333;
            margin-bottom: 30px;
            font-size: 28px;
            text-align: center;
        }
        .tabs {
            display: flex;
            gap: 10px;
            margin-bottom: 30px;
            border-bottom: 2px solid #e0e0e0;
        }
        .tab {
            flex: 1;
            padding: 12px;
            border: none;
            background: none;
            cursor: pointer;
            font-size: 16px;
            font-weight: 500;
            color: #888;
            border-bottom: 2px solid transparent;
            margin-bottom: -2px;
        }
        .tab.active {
            color: #667eea;
            border-bottom-color: #667eea;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #555;
            font-weight: 500;
        }
        input[type="text"],
        input[type="email"],
        input[type="password"] {
            width: 100%;
            padding: 12px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input[type="text"]:focus,
        input[type="email"]:focus,
        input[type="password"]:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            width: 100%;
            padding: 14px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        }
        button:active {
            transform: translateY(0);
        }
        .message {
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            font-weight: 500;
        }
        .message.error {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .message.success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .help-text {
            font-size: 14px;
            color: #888;
            margin-top: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🏭 Smart-Column S3</h1>
        
        <?php if ($error): ?>
            <div class="message error"><?php echo htmlspecialchars($error); ?></div>
        <?php endif; ?>
        
        <?php if ($success): ?>
            <div class="message success"><?php echo htmlspecialchars($success); ?></div>
        <?php endif; ?>
        
        <div class="tabs">
            <button class="tab active" onclick="showTab('login')">Вход</button>
            <button class="tab" onclick="showTab('register')">Регистрация</button>
        </div>
        
        <!-- Форма входа -->
        <div id="login-tab" class="tab-content active">
            <form method="POST">
                <input type="hidden" name="action" value="login">
                <div class="form-group">
                    <label for="login-username">Логин</label>
                    <input type="text" id="login-username" name="username" required autofocus>
                </div>
                <div class="form-group">
                    <label for="login-password">Пароль</label>
                    <input type="password" id="login-password" name="password" required>
                </div>
                <button type="submit">Войти</button>
            </form>
            <div class="help-text" style="margin-top: 15px; text-align: center;">
                По умолчанию: admin / admin
            </div>
        </div>
        
        <!-- Форма регистрации -->
        <div id="register-tab" class="tab-content">
            <form method="POST">
                <input type="hidden" name="action" value="register">
                <div class="form-group">
                    <label for="reg-username">Логин</label>
                    <input type="text" id="reg-username" name="username" required minlength="3">
                    <div class="help-text">Минимум 3 символа</div>
                </div>
                <div class="form-group">
                    <label for="reg-email">Email (необязательно)</label>
                    <input type="email" id="reg-email" name="email">
                </div>
                <div class="form-group">
                    <label for="reg-password">Пароль</label>
                    <input type="password" id="reg-password" name="password" required minlength="6">
                    <div class="help-text">Минимум 6 символов</div>
                </div>
                <button type="submit">Зарегистрироваться</button>
            </form>
        </div>
    </div>
    
    <script>
        function showTab(tab) {
            // Убрать active со всех табов
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            
            // Активировать выбранный таб
            event.target.classList.add('active');
            document.getElementById(tab + '-tab').classList.add('active');
        }
    </script>
</body>
</html>

