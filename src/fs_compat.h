// Совместимость с SPIFFS - используем LittleFS
// LittleFS более надежна и поддерживает длинные имена файлов
#ifndef FS_COMPAT_H
#define FS_COMPAT_H

#include <LittleFS.h>

// Алиас для совместимости со старым кодом
#define SPIFFS LittleFS

#endif // FS_COMPAT_H
