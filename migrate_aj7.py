import os
import re

def migrate_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. Replace Document types
    content = re.sub(r'StaticJsonDocument<\s*\d+\s*>\s+(\w+)\s*;', r'JsonDocument \1;', content)
    content = re.sub(r'DynamicJsonDocument\s+(\w+)\s*\(\s*\d+\s*\)\s*;', r'JsonDocument \1;', content)
    content = re.sub(r'DynamicJsonDocument\s+(\w+)\s*\(\s*docSize\s*\)\s*;', r'JsonDocument \1;', content)

    # 2. Replace createNestedObject with key
    content = re.sub(r'\.createNestedObject\s*\(\s*([^)]+)\s*\)', r'[\1].to<JsonObject>()', content)
    
    # 3. Replace createNestedArray with key
    content = re.sub(r'\.createNestedArray\s*\(\s*([^)]+)\s*\)', r'[\1].to<JsonArray>()', content)

    # 4. Replace createNestedObject without key (inside arrays)
    content = content.replace('.createNestedObject()', '.add<JsonObject>()')
    content = content.replace('.createNestedArray()', '.add<JsonArray>()')

    # 5. Replace containsKey with is<JsonVariant>()
    # Example: doc.containsKey("key") -> doc["key"].is<JsonVariant>()
    content = re.sub(r'\.containsKey\s*\(\s*([^)]+)\s*\)', r'[\1].is<JsonVariant>()', content)

    # 6. Fix double brackets if any (e.g. [["key"]].to<JsonObject>())
    content = content.replace('[[', '[').replace(']]', ']')

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)

def main():
    src_dir = 'src'
    for root, dirs, files in os.walk(src_dir):
        for file in files:
            if file.endswith(('.cpp', '.h')):
                migrate_file(os.path.join(root, file))

if __name__ == '__main__':
    main()
