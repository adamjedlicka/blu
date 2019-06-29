#ifndef blu_object_h
#define blu_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)

#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_ARRAY(value) ((ObjArray*)AS_OBJ(value))

typedef enum {
	OBJ_ARRAY,
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_NATIVE,
	OBJ_STRING,
	OBJ_UPVALUE,
} ObjType;

struct sObj {
	ObjType type;
	struct sObj* next;
};

typedef struct {
	Obj obj;
	int length;
	char* chars;
	uint32_t hash;
} ObjString;

typedef struct {
	Obj obj;
	uint32_t len;
	uint32_t cap;
	Value* data;
} ObjArray;

typedef struct {
	Obj obj;
	int arity;
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
	Obj obj;
	int arity;
	NativeFn function;
} ObjNative;

typedef struct sUpvalue {
	Obj obj;

	// Pointer to the variable this upvalue is referencing.
	Value* value;

	// If the upvalue is closed (i.e. the local variable it was pointing to has been popped off the stack) then the
	// closed-over value is hoisted out of the stack into here. [value] is then be changed to point to this.
	Value closed;

	// Open upvalues are stored in a linked list. This points to the next one in that list.
	struct sUpvalue* next;
} ObjUpvalue;

typedef struct {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;

ObjArray* newArray(uint32_t len);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function, int arity);
ObjUpvalue* newUpvalue(Value* slot);

void arrayPush(ObjArray*, Value);

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
