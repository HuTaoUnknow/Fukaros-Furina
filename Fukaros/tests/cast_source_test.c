#include "cast_source.h"
#include <stdio.h>
#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            fprintf(stderr, "Failed: %s, line %d\n", #condition, __LINE__);                                  \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)

int main(void) {
    const fr_cast_display main = {0, 0, 1920, 1080, 1};
    const fr_cast_display right = {1920, 0, 2560, 1440, 0};
    const fr_cast_display left = {-1280, -200, 1280, 800, 0};
    const fr_cast_display above = {0, -1080, 1920, 1080, 0};
    const fr_cast_display clone = {0, 0, 1920, 1080, 0};
    const fr_cast_display overlap = {1900, 0, 1920, 1080, 0};
    const fr_cast_display empty = {0, 0, 0, 1080, 0};
    const fr_cast_display far = {INT32_MAX - 100, 0, 1920, 1080, 0};
    CHECK(fr_cast_source_usable(FR_CAST_DUPLICATE, &main, &main));
    CHECK(fr_cast_source_usable(FR_CAST_DUPLICATE, &right, &main));
    CHECK(!fr_cast_source_usable(FR_CAST_EXTEND, &main, &main));
    CHECK(fr_cast_source_usable(FR_CAST_EXTEND, &right, &main));
    CHECK(fr_cast_source_usable(FR_CAST_EXTEND, &left, &main));
    CHECK(fr_cast_source_usable(FR_CAST_EXTEND, &above, &main));
    CHECK(!fr_cast_source_usable(FR_CAST_EXTEND, &clone, &main));
    CHECK(!fr_cast_source_usable(FR_CAST_EXTEND, &overlap, &main));
    CHECK(!fr_cast_source_usable(FR_CAST_DUPLICATE, &empty, &main));
    CHECK(!fr_cast_source_usable(FR_CAST_EXTEND, &right, 0));
    CHECK(!fr_cast_source_usable(FR_CAST_DUPLICATE, 0, &main));
    CHECK(!fr_cast_source_usable((fr_cast_mode)2, &right, &main));
    CHECK(fr_cast_source_usable(FR_CAST_EXTEND, &far, &main));
    CHECK(fr_cast_source_usable(FR_CAST_EXTEND, &main, &far) == 0);
    return 0;
}
