#ifndef blu_object_h
#define blu_object_h

#include "blu.h"
#include "compiler/chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) bluIsObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) bluIsObjType(value, OBJ_CLASS)
#define IS_FUNCTION(value) bluIsObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) bluIsObjType(value, OBJ_INSTANCE)
#define IS_STRING(value) bluIsObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((bluObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value) ((bluObjClass*)AS_OBJ(value))
#define AS_FUNCTION(value) ((bluObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((bluObjInstance*)AS_OBJ(value))
#define AS_STRING(value) ((bluObjString*)AS_OBJ(value))

#define AS_CSTRING(value) (((bluObjString*)AS_OBJ(value))->chars)

typedef struct bluObjBoundMethod bluObjBoundMethod;
typedef struct bluObjClass bluObjClass;
typedef struct bluObjFunction bluObjFunction;
typedef struct bluObjInstance bluObjInstance;
typedef struct bluObjUpvalue bluObjUpvalue;

DECLARE_BUFFER(bluObjUpvalue, bluObjUpvalue*);

typedef enum {
	OBJ_BOUND_METHOD,
	OBJ_CLASS,
	OBJ_FUNCTION,
	OBJ_INSTANCE,
	OBJ_STRING,
	OBJ_UPVALUE,
} bluObjType;

struct bluObj {
	bluObjType type;
	bluObjClass* class;

	bool isDark;
	bluObj* next;
};

struct bluObjBoundMethod {
	bluObj obj;
	bluValue receiver;
	bluObjFunction* function;
};

struct bluObjClass {
	bluObj obj;
	bluObjString* name;
	bluObjClass* superclass;
	bluTable methods;
};

struct bluObjFunction {
	bluObj obj;
	int8_t arity;
	bluChunk chunk;
	bluObjUpvalueBuffer upvalues;
	bluObjString* name;
};

struct bluObjInstance {
	bluObj obj;
	bluTable fields;
};

struct bluObjString {
	bluObj obj;
	int32_t length;
	char* chars;
	int32_t hash;
};

struct bluObjUpvalue {
	bluObj obj;

	// Pointer to the variable this upvalue is referencing.
	bluValue* value;

	// If the upvalue is closed (i.e. the local variable it was pointing to has been popped off the stack) then the
	// closed-over value is hoisted out of the stack into here. [value] is then be changed to point to this.
	bluValue closed;

	// Open upvalues are stored in a linked list. This points to the next one in that list.
	bluObjUpvalue* next;
};

bluObjBoundMethod* bluNewBoundMethod(bluVM* vm, bluValue receiver, bluObjFunction* function);
bluObjClass* bluNewClass(bluVM* vm, bluObjString* name);
bluObjFunction* bluNewFunction(bluVM* vm);
bluObjInstance* newInstance(bluVM* vm, bluObjClass* class);
bluObjUpvalue* newUpvalue(bluVM* vm, bluValue* slot);

bluObjString* bluCopyString(bluVM* vm, const char* chars, int32_t length);
bluObjString* bluTakeString(bluVM* vm, char* chars, int32_t length);

void bluPrintObject(bluValue value);

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
