#ifndef blu_object_h
#define blu_object_h

#include <stdint.h>

#include "blu.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

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

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
