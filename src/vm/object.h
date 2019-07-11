#ifndef blu_object_h
#define blu_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_ARRAY(value) bluIsObjType(value, OBJ_ARRAY)
#define IS_BOUND_METHOD(value) bluIsObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) bluIsObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) bluIsObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) bluIsObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) bluIsObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) bluIsObjType(value, OBJ_NATIVE)
#define IS_STRING(value) bluIsObjType(value, OBJ_STRING)

#define AS_ARRAY(value) ((bluObjArray*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((bluObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value) ((bluObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) ((bluObjClosure*)AS_OBJ(value))
#define AS_CSTRING(value) (((bluObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((bluObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((bluObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) (((bluObjNative*)AS_OBJ(value)))
#define AS_STRING(value) ((bluObjString*)AS_OBJ(value))

typedef struct _ObjArray bluObjArray;
typedef struct _ObjBoundMethod bluObjBoundMethod;
typedef struct _ObjClass bluObjClass;
typedef struct _ObjClosure bluObjClosure;
typedef struct _ObjFunction bluObjFunction;
typedef struct _ObjInstance bluObjInstance;
typedef struct _ObjNative bluObjNative;
typedef struct _ObjUpvalue bluObjUpvalue;

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
} bluObjType;

struct _Obj {
	bluObjType type;
	bluObjClass* klass;
	bool isDark;
	struct _Obj* next;
};

struct _ObjArray {
	bluObj obj;
	uint32_t len;
	uint32_t cap;
	bluValue* data;
};

struct _ObjBoundMethod {
	bluObj obj;
	bluValue receiver;
	bluObjClosure* method;
};

struct _ObjClass {
	bluObj obj;
	bluObjString* name;
	bluTable methods;
	bluObjClass* superclass;
};

struct _ObjClosure {
	bluObj obj;
	bluObjFunction* function;
	bluObjUpvalue** upvalues;
	int upvalueCount;
};

struct _ObjFunction {
	bluObj obj;
	int arity;
	int upvalueCount;
	bluChunk chunk;
	bluObjString* name;
};

struct _ObjInstance {
	bluObj obj;
	bluTable fields;
};

typedef bluValue (*bluNativeFn)(bluVM* vm, int argCount, bluValue* args);

struct _ObjNative {
	bluObj obj;
	int arity;
	bluNativeFn function;
};

struct _ObjString {
	bluObj obj;
	int length;
	char* chars;
	uint32_t hash;
};

struct _ObjUpvalue {
	bluObj obj;

	// Pointer to the variable this upvalue is referencing.
	bluValue* value;

	// If the upvalue is closed (i.e. the local variable it was pointing to has been popped off the stack) then the
	// closed-over value is hoisted out of the stack into here. [value] is then be changed to point to this.
	bluValue closed;

	// Open upvalues are stored in a linked list. This points to the next one in that list.
	bluObjUpvalue* next;
};

bluObjArray* bluNewArray(bluVM*, uint32_t len);
bluObjBoundMethod* bluNewBoundMethod(bluVM*, bluValue receiver, bluObjClosure* method);
bluObjClass* bluNewClass(bluVM*, bluObjString* name);
bluObjClosure* bluNewClosure(bluVM*, bluObjFunction* function);
bluObjFunction* bluNewFunction(bluVM*);
bluObjInstance* bluNewInstance(bluVM*, bluObjClass* klass);
bluObjNative* bluNewNative(bluVM*, bluNativeFn function, int arity);
bluObjUpvalue* bluNewUpvalue(bluVM*, bluValue* slot);

void bluArrayPush(bluVM* vm, bluObjArray*, bluValue);

bluObjString* bluTakeString(bluVM* vm, char* chars, int length);
bluObjString* bluCopyString(bluVM* vm, const char* chars, int length);

void bluPrintObject(bluVM* vm, bluValue value);

static inline bool bluIsObjType(bluValue value, bluObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
