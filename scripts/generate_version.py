#!/usr/bin/env python3
"""
Генерирует файл version.json с датой и временем сборки фронтенда
"""
from SCons.Script import Import
Import("env")
import json
from datetime import datetime

def generate_version_json(source, target, env):
    """Генерация version.json перед сборкой файловой системы"""

    version_data = {
        "buildDate": datetime.now().strftime("%b %d %Y"),
        "buildTime": datetime.now().strftime("%H:%M:%S"),
        "buildTimestamp": int(datetime.now().timestamp()),
        "builder": "PlatformIO"
    }

    version_file = "data/version.json"

    with open(version_file, 'w') as f:
        json.dump(version_data, f, indent=2)

    print(f"✓ Generated {version_file}")
    print(f"  Build Date: {version_data['buildDate']}")
    print(f"  Build Time: {version_data['buildTime']}")

# Регистрируем хук перед сборкой файловой системы
env.AddPreAction("$BUILD_DIR/littlefs.bin", generate_version_json)
