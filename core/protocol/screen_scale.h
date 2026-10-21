#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int fr_screen_scale_valid(unsigned percent);
/* Translate an absolute scale into the source-relative Windows DPI slot.
 * Reject unsupported slots rather than silently changing a requested value. */
int fr_screen_scale_relative(unsigned percent, int minimum_relative, int maximum_relative, int* relative);
int fr_screen_scale_absolute(int minimum_relative, int current_relative);
#ifdef __cplusplus
}
#endif
