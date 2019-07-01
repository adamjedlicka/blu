#ifndef blu_common_h
#define blu_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STRINGIFY(x) #x
#define STR(x) STRINGIFY(x)

#define BLU_VERSION_MAJOR 0
#define BLU_VERSION_MINOR 2
#define BLU_VERSION_PATCH 0

#define BLU_VERSION BLU_VERSION_MAJOR * 1000000 + BLU_VERSION_MINOR * 1000 + BLU_VERSION_MINOR

#define BLU_VERSION_STR "v" STR(BLU_VERSION_MAJOR) "." STR(BLU_VERSION_MINOR) "." STR(BLU_VERSION_PATCH)

#ifdef DEBUG
#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION
#define DEBUG_TRACE_GC
#define DEBUG_STRESS_GC
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
