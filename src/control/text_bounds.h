#pragma once

#include <stddef.h>

// Ограничение текста по размеру буфера без разрыва многобайтового символа.
//
// Зачем отдельно: snprintf с буфером, меньшим худшего случая, режет строку по
// байтам. Для латиницы это потеря хвоста, а для кириллицы — половина символа:
// на экране и в логе появляется «?» или мусор, и сообщение об аварии читается
// как повреждённое. Здесь усечение всегда попадает на границу символа.
//
// Заголовок намеренно без Arduino: он проверяется нативными тестами.

namespace TextBounds {

// Сколько байт из text можно взять, не превысив maxBytes и не разорвав символ.
// Возвращает длину в байтах; завершающий ноль не входит.
inline size_t utf8SafeLength(const char *text, size_t maxBytes) {
  if (text == nullptr || maxBytes == 0) {
    return 0;
  }

  size_t length = 0;
  while (length < maxBytes && text[length] != '\0') {
    ++length;
  }
  if (text[length] == '\0') {
    return length; // строка уместилась целиком
  }

  // Байт на границе разреза может быть продолжением символа (10xxxxxx).
  // Отступаем на начало символа, иначе последний символ будет неполным.
  while (length > 0 &&
         (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
    --length;
  }
  return length;
}

// Копирует не больше destSize-1 байт, не разрывая символ. Возвращает длину.
inline size_t copyUtf8(char *dest, size_t destSize, const char *src) {
  if (dest == nullptr || destSize == 0) {
    return 0;
  }
  if (src == nullptr) {
    dest[0] = '\0';
    return 0;
  }

  const size_t length = utf8SafeLength(src, destSize - 1);
  for (size_t i = 0; i < length; ++i) {
    dest[i] = src[i];
  }
  dest[length] = '\0';
  return length;
}

} // namespace TextBounds