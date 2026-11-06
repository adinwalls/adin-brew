/* Let binaries built on glibc >= 2.38 load on older distros (e.g. Ubuntu/Pop!_OS 22.04).
   GCC 13 + glibc 2.38 headers redirect strtol & co. to C23 variants; route them back. */
#undef _GNU_SOURCE
#include <stdlib.h>
#define HIDDEN __attribute__((visibility("hidden")))
HIDDEN long __isoc23_strtol(const char* s, char** e, int b) { return strtol(s, e, b); }
HIDDEN unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return strtoul(s, e, b); }
HIDDEN long long __isoc23_strtoll(const char* s, char** e, int b) { return strtoll(s, e, b); }
HIDDEN unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return strtoull(s, e, b); }
