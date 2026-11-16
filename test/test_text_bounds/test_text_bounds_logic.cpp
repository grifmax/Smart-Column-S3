// Границы усечения текста по размеру буфера.
//
// Повод: сообщения прибора на русском. snprintf с малым буфером режет строку
// по байтам, а кириллица двухбайтовая — половина буквы на экране читается как
// «?» или мусор. Проверяем, что усечение всегда попадает на границу символа.

#include <unity.h>

#include <string.h>

#include <../../src/control/text_bounds.h>

void setUp() {}
void tearDown() {}

// Строка должна остаться корректным UTF-8: ни одного «сиротского» байта
// продолжения и ни одного обрезанного ведущего байта.
static bool isValidUtf8(const char *text) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
  while (*p != 0) {
    const unsigned char lead = *p++;
    size_t extra = 0;
    if (lead < 0x80) {
      extra = 0;
    } else if ((lead & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false; // байт продолжения в роли ведущего
    }
    for (size_t i = 0; i < extra; ++i) {
      if (*p == 0 || (*p & 0xC0) != 0x80) {
        return false;
      }
      ++p;
    }
  }
  return true;
}

void test_short_text_is_copied_whole() {
  char dest[16];
  const size_t length = TextBounds::copyUtf8(dest, sizeof(dest), "abc");
  TEST_ASSERT_EQUAL_UINT32(3, length);
  TEST_ASSERT_EQUAL_STRING("abc", dest);
}

void test_text_that_fits_exactly_is_not_cut() {
  char dest[7];
  const size_t length = TextBounds::copyUtf8(dest, sizeof(dest), "abcdef");
  TEST_ASSERT_EQUAL_UINT32(6, length);
  TEST_ASSERT_EQUAL_STRING("abcdef", dest);
}

void test_ascii_is_cut_at_buffer_limit() {
  char dest[4];
  const size_t length = TextBounds::copyUtf8(dest, sizeof(dest), "abcdef");
  TEST_ASSERT_EQUAL_UINT32(3, length);
  TEST_ASSERT_EQUAL_STRING("abc", dest);
}

void test_cyrillic_cut_keeps_whole_characters() {
  // «абв» — по два байта на букву. Пять доступных байт не должны дать
  // полторы буквы: остаются ровно две целые.
  char dest[6];
  const size_t length = TextBounds::copyUtf8(dest, sizeof(dest), "абв");
  TEST_ASSERT_EQUAL_UINT32(4, length);
  TEST_ASSERT_EQUAL_STRING("аб", dest);
  TEST_ASSERT_TRUE(isValidUtf8(dest));
}

void test_cyrillic_never_leaves_partial_character() {
  // Перебираем все размеры буфера: результат обязан быть корректным UTF-8.
  const char *text = "Проверка датчика куба";
  for (size_t size = 1; size <= 32; ++size) {
    char dest[33];
    const size_t length = TextBounds::copyUtf8(dest, size, text);
    TEST_ASSERT_TRUE_MESSAGE(isValidUtf8(dest), "усечение разорвало символ");
    TEST_ASSERT_EQUAL_UINT32(length, strlen(dest));
    TEST_ASSERT_TRUE(length < size);
    TEST_ASSERT_EQUAL_INT(0, strncmp(dest, text, length));
  }
}

void test_four_byte_character_is_dropped_not_split() {
  // Эмодзи занимает 4 байта; при трёх доступных не остаётся ничего.
  char dest[4];
  const size_t length = TextBounds::copyUtf8(dest, sizeof(dest), "\xF0\x9F\x98\x80");
  TEST_ASSERT_EQUAL_UINT32(0, length);
  TEST_ASSERT_EQUAL_STRING("", dest);

  char wide[5];
  const size_t full = TextBounds::copyUtf8(wide, sizeof(wide), "\xF0\x9F\x98\x80");
  TEST_ASSERT_EQUAL_UINT32(4, full);
  TEST_ASSERT_TRUE(isValidUtf8(wide));
}

void test_zero_and_null_sizes_are_safe() {
  char dest[4] = "xyz";
  TEST_ASSERT_EQUAL_UINT32(0, TextBounds::copyUtf8(dest, 0, "abc"));
  TEST_ASSERT_EQUAL_UINT32(0, TextBounds::copyUtf8(dest, 1, "abc"));
  TEST_ASSERT_EQUAL_STRING("", dest);

  char other[4] = "xyz";
  TEST_ASSERT_EQUAL_UINT32(0, TextBounds::copyUtf8(other, sizeof(other), nullptr));
  TEST_ASSERT_EQUAL_STRING("", other);
}

void test_safe_length_never_splits_character() {
  TEST_ASSERT_EQUAL_UINT32(0, TextBounds::utf8SafeLength("аб", 1));
  TEST_ASSERT_EQUAL_UINT32(2, TextBounds::utf8SafeLength("аб", 2));
  TEST_ASSERT_EQUAL_UINT32(2, TextBounds::utf8SafeLength("аб", 3));
  TEST_ASSERT_EQUAL_UINT32(4, TextBounds::utf8SafeLength("аб", 4));
  TEST_ASSERT_EQUAL_UINT32(4, TextBounds::utf8SafeLength("аб", 99));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_short_text_is_copied_whole);
  RUN_TEST(test_text_that_fits_exactly_is_not_cut);
  RUN_TEST(test_ascii_is_cut_at_buffer_limit);
  RUN_TEST(test_cyrillic_cut_keeps_whole_characters);
  RUN_TEST(test_cyrillic_never_leaves_partial_character);
  RUN_TEST(test_four_byte_character_is_dropped_not_split);
  RUN_TEST(test_zero_and_null_sizes_are_safe);
  RUN_TEST(test_safe_length_never_splits_character);
  return UNITY_END();
}