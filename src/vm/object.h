#ifndef blu_object_h
#define blu_object_h

#include "blu.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) bluIsObjType(value, OBJ_STRING)

#define AS_CSTRING(value) (((bluObjString*)AS_OBJ(value))->chars)
#define AS_STRING(value) ((bluObjString*)AS_OBJ(value))

typedef enum {
	OBJ_STRING,
} bluObjType;

struct bluObj {
	bluObjType type;
};

struct bluObjString {
	bluObj obj;
	uint32_t length;
	char* chars;
	uint32_t hash;
};

bluObjString* bluTakeString(bluVM* vm, char* chars, uint32_t length);
bluObjString* bluCopyString(bluVM* vm, const char* chars, uint32_t length);

void bluPrintObject(bluValue value);

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
