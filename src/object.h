#ifndef blu_object_h
#define blu_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_ARRAY(value) ((ObjArray*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value)))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))

typedef struct sObjArray ObjArray;
typedef struct sObjBoundMethod ObjBoundMethod;
typedef struct sObjClass ObjClass;
typedef struct sObjClosure ObjClosure;
typedef struct sObjFunction ObjFunction;
typedef struct sObjInstance ObjInstance;
typedef struct sObjNative ObjNative;
typedef struct sObjUpvalue ObjUpvalue;

typedef enum {
	OBJ_ARRAY,
	OBJ_BOUND_METHOD,
	OBJ_CLASS,
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_INSTANCE,
	OBJ_NATIVE,
	OBJ_STRING,
	OBJ_UPVALUE,
} ObjType;

struct sObj {
	ObjType type;
	bool isDark;
	struct sObj* next;
};

struct sObjArray {
	Obj obj;
	uint32_t len;
	uint32_t cap;
	Value* data;
};

struct sObjBoundMethod {
	Obj obj;
	Value receiver;
	ObjClosure* method;
};

struct sObjClass {
	Obj obj;
	ObjString* name;
	Table methods;
};

struct sObjClosure {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
};

struct sObjFunction {
	Obj obj;
	int arity;
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
};

struct sObjInstance {
	Obj obj;
	ObjClass* klass;
	Table fields;
};

typedef Value (*NativeFn)(int argCount, Value* args);

struct sObjNative {
	Obj obj;
	int arity;
	NativeFn function;
};

struct sObjString {
	Obj obj;
	int length;
	char* chars;
	uint32_t hash;
};

struct sObjUpvalue {
	Obj obj;

	// Pointer to the variable this upvalue is referencing.
	Value* value;

	// If the upvalue is closed (i.e. the local variable it was pointing to has been popped off the stack) then the
	// closed-over value is hoisted out of the stack into here. [value] is then be changed to point to this.
	Value closed;

	// Open upvalues are stored in a linked list. This points to the next one in that list.
	ObjUpvalue* next;
};

ObjArray* newArray(uint32_t len);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
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
