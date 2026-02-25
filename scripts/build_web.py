#!/usr/bin/env python3
"""
Pre-script: собирает JS бандл из src/web/ и создаёт gzip-версию.
Запускается автоматически перед сборкой LittleFS.
"""
from SCons.Script import Import
Import("env")
import subprocess
import gzip
import os
import shutil

def build_web_bundle(source, target, env):
    """Сборка JS бандла через ESBuild"""
    project_dir = env.get("PROJECT_DIR", ".")

    # Проверяем, есть ли node_modules
    node_modules = os.path.join(project_dir, "node_modules")
    if not os.path.isdir(node_modules):
        print("⚠ node_modules not found, running npm install...")
        subprocess.run(["npm", "install"], cwd=project_dir, check=True, shell=True)

    # Собираем бандл
    print("🔨 Building web bundle...")
    result = subprocess.run(["npm", "run", "build"], cwd=project_dir, capture_output=True, text=True, shell=True)
    if result.returncode != 0:
        print(f"❌ Build failed:\n{result.stderr}")
        raise Exception("Web bundle build failed")
    print(result.stdout.strip())

    # Gzip сжатие ключевых файлов
    data_dir = os.path.join(project_dir, "data")
    gzip_files = ["app.js", "style.css", "index.html"]

    for fname in gzip_files:
        src_path = os.path.join(data_dir, fname)
        gz_path = src_path + ".gz"
        if os.path.isfile(src_path):
            with open(src_path, 'rb') as f_in:
                with gzip.open(gz_path, 'wb', compresslevel=9) as f_out:
                    shutil.copyfileobj(f_in, f_out)
            src_size = os.path.getsize(src_path)
            gz_size = os.path.getsize(gz_path)
            ratio = (1 - gz_size / src_size) * 100 if src_size > 0 else 0
            print(f"  📦 {fname}: {src_size/1024:.1f}KB → {gz_size/1024:.1f}KB ({ratio:.0f}% saved)")

# Регистрируем хук перед сборкой файловой системы
env.AddPreAction("$BUILD_DIR/littlefs.bin", build_web_bundle)
