#ifndef blu_h
#define blu_h

#define _GNU_SOURCE

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STRINGIFY(x) #x
#define STR(x) STRINGIFY(x)

#define BLU_VERSION_MAJOR 0
#define BLU_VERSION_MINOR 6
#define BLU_VERSION_PATCH 0

#define BLU_VERSION BLU_VERSION_MAJOR * 1000000 + BLU_VERSION_MINOR * 1000 + BLU_VERSION_PATCH

#define BLU_VERSION_STR "v" STR(BLU_VERSION_MAJOR) "." STR(BLU_VERSION_MINOR) "." STR(BLU_VERSION_PATCH)

typedef struct bluVM bluVM;

typedef struct bluValue bluValue;

typedef struct bluObj bluObj;

typedef struct bluObjString bluObjString;

typedef int8_t (*bluNativeFn)(bluVM* vm, int8_t argCount, bluValue* args);

typedef void (*bluModuleLoader)(bluVM* vm);

typedef struct {
	bluObjString* name;
	bluModuleLoader loader;
	char* source;
	bool loaded;
} bluModule;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
	INTERPRET_ASSERTION_ERROR,
} bluInterpretResult;

bluVM* bluNewVM();

void bluFreeVM(bluVM* vm);

bluInterpretResult bluInterpret(bluVM* vm, const char* source, const char* name);

bluObj* bluGetGlobal(bluVM* vm, const char* name);

bool bluDefineMethod(bluVM* vm, bluObj* obj, const char* name, bluNativeFn function, int8_t arity);
bool bluDefineStaticMethod(bluVM* vm, bluObj* obj, const char* name, bluNativeFn function, int8_t arity);

void bluRegisterModule(bluVM* vm, const char* name, bluModuleLoader loader);

#endif
