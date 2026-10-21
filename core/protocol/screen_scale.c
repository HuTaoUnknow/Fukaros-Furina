#include "screen_scale.h"
#include <stddef.h>
static const int scales[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};

int fr_screen_scale_valid(unsigned percent) {
    for (unsigned i = 0; i < 8; i++)
        if (percent == (unsigned)scales[i])
            return 1;
    return 0;
}

int fr_screen_scale_absolute(int minimum_relative, int current_relative) {
    if (minimum_relative > 0 || minimum_relative < -11 || current_relative < minimum_relative ||
        current_relative > 11 || current_relative - minimum_relative > 11)
        return 0;
    return scales[current_relative - minimum_relative];
}

int fr_screen_scale_relative(unsigned percent, int minimum_relative, int maximum_relative, int* relative) {
    if (!relative || !fr_screen_scale_valid(percent) || minimum_relative > 0 || minimum_relative < -11 ||
        maximum_relative < 0 || maximum_relative > 11 || maximum_relative - minimum_relative > 11)
        return 0;
    for (int i = 0; i < 8; i++)
        if ((unsigned)scales[i] == percent && i + minimum_relative <= maximum_relative) {
            *relative = i + minimum_relative;
            return 1;
        }
    return 0;
}
