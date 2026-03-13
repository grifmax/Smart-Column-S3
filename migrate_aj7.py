import os
import re

def migrate_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original_content = content

    # 1. StaticJsonDocument<N> doc -> JsonDocument doc
    content = re.sub(r'StaticJsonDocument<\s*[\w\+\-\*/\s]+\s*>\s+(\w+);', r'JsonDocument \1;', content)
    
    # 2. DynamicJsonDocument doc(N) -> JsonDocument doc
    content = re.sub(r'DynamicJsonDocument\s+(\w+)\(\s*[\w\+\-\*/\s]+\s*\);', r'JsonDocument \1;', content)

    # 3. doc.createNestedObject("key") -> doc["key"].to<JsonObject>()
    content = re.sub(r'(\w+)\.createNestedObject\(\s*"([^"]+)"\s*\)', r'\1["\2"].to<JsonObject>()', content)
    
    # 4. doc.createNestedArray("key") -> doc["key"].to<JsonArray>()
    content = re.sub(r'(\w+)\.createNestedArray\(\s*"([^"]+)"\s*\)', r'\1["\2"].to<JsonArray>()', content)

    # 5. array.createNestedObject() -> array.add<JsonObject>()
    content = re.sub(r'(\w+)\.createNestedObject\(\)', r'\1.add<JsonObject>()', content)
    
    # 6. array.createNestedArray() -> array.add<JsonArray>()
    content = re.sub(r'(\w+)\.createNestedArray\(\)', r'\1.add<JsonArray>()', content)

    # 7. containsKey -> is<JsonVariant>() или просто [] (заменяем на .containsKey -> [])
    # В ArduinoJson 7 containsKey устарел. Обычно используется doc["key"].is<T>()
    # Мы заменим doc.containsKey("key") на !doc["key"].isNull()
    content = re.sub(r'(\w+)\.containsKey\(\s*"([^"]+)"\s*\)', r'!\1["\2"].isNull()', content)

    if content != original_content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        return True
    return False

def main():
    targets = ['src']
    count = 0
    for target in targets:
        for root, dirs, files in os.walk(target):
            for file in files:
                if file.endswith(('.cpp', '.h')):
                    if migrate_file(os.path.join(root, file)):
                        print(f"Migrated: {os.path.join(root, file)}")
                        count += 1
    print(f"Total files migrated: {count}")

if __name__ == "__main__":
    main()
