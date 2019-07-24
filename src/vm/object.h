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

typedef struct bluObjFunction bluObjFunction;
typedef struct bluObjUpvalue bluObjUpvalue;

DECLARE_BUFFER(bluObjUpvalue, bluObjUpvalue*);

typedef enum {
	OBJ_FUNCTION,
	OBJ_STRING,
	OBJ_UPVALUE,
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

struct bluObjFunction {
	bluObj obj;
	int8_t arity;
	bluChunk chunk;
	bluObjUpvalueBuffer upvalues;
	bluObjString* name;
};

struct bluObjUpvalue {
	bluObj obj;

	// Stack offset to the variable this upvalue is referencing.
	int32_t stackOffset;

	// If the upvalue is closed (i.e. the local variable it was pointing to has been popped off the stack) then the
	// closed-over value is hoisted out of the stack into here. [value] is then be changed to point to this.
	bluValue closed;

	// Open upvalues are stored in a linked list. This points to the next one in that list.
	bluObjUpvalue* next;
};

bluObjFunction* bluNewFunction(bluVM* vm);
bluObjUpvalue* newUpvalue(bluVM* vm, int32_t stackOffset);

bluObjString* bluCopyString(bluVM* vm, const char* chars, int32_t length);
bluObjString* bluTakeString(bluVM* vm, char* chars, int32_t length);

void bluPrintObject(bluValue value);

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
