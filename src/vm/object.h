#ifndef blu_object_h
#define blu_object_h

#include "blu.h"
#include "compiler/chunk.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) bluIsObjType(value, OBJ_FUNCTION)
#define IS_STRING(value) bluIsObjType(value, OBJ_STRING)

#define AS_CSTRING(value) (((bluObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((bluObjFunction*)AS_OBJ(value))
#define AS_STRING(value) ((bluObjString*)AS_OBJ(value))

typedef enum {
	OBJ_FUNCTION,
	OBJ_STRING,
} bluObjType;

struct bluObj {
	bluObjType type;

	bool isDark;
	bluObj* next;
};

struct bluObjString {
	bluObj obj;
	int32_t length;
	char* chars;
	int32_t hash;
};

typedef struct {
	bluObj obj;
	int8_t arity;
	bluChunk chunk;
	bluObjString* name;
} bluObjFunction;

bluObjFunction* bluNewFunction(bluVM* vm);
bluObjString* bluCopyString(bluVM* vm, const char* chars, int32_t length);
bluObjString* bluTakeString(bluVM* vm, char* chars, int32_t length);

void bluPrintObject(bluValue value);

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
