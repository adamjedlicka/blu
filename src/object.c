#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;
	object->isDark = false;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_TRACE_GC
	printf("%p allocate %ld for %d\n", object, size, type);
#endif

	return object;
}

ObjArray* newArray(uint32_t len) {
	// Set cap to the closest higher power of two.
	uint32_t cap = len;
	cap |= cap >> 1;
	cap |= cap >> 2;
	cap |= cap >> 4;
	cap++;

	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->obj.klass = vm.arrayClass;
	array->cap = 0;
	array->len = 0;

	// Push the array to stack so GC won't collect it
	push(OBJ_VAL(array));

	array->data = ALLOCATE(Value, cap);
	array->cap = cap;
	array->len = len;

	pop();

	return array;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->obj.klass = method->obj.klass;
	bound->receiver = receiver;
	bound->method = method;

	return bound;
}

ObjClass* newClass(ObjString* name) {
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->obj.klass = vm.classClass;
	klass->name = name;
	klass->superclass = NULL;
	initTable(&klass->methods);
	return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
	// Allocate the upvalue array first so it doesn't cause the closure to get collected.
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->obj.klass = function->obj.klass;
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;

	return closure;
}

ObjFunction* newFunction() {
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->obj.klass = vm.functionClass;
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;

	initChunk(&function->chunk);

	return function;
}

ObjInstance* newInstance(ObjClass* klass) {
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->obj.klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjNative* newNative(NativeFn function, int arity) {
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->obj.klass = vm.functionClass;
	native->function = function;
	native->arity = arity;

	return native;
}

ObjUpvalue* newUpvalue(Value* slot) {
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->obj.klass = getClass(*slot);
	upvalue->closed = NIL_VAL;
	upvalue->value = slot;
	upvalue->next = NULL;

	return upvalue;
}

void arrayPush(ObjArray* array, Value value) {
	if (array->len == array->cap) {
		int cap = GROW_CAPACITY(array->cap);
		array->data = GROW_ARRAY(array->data, Value, array->cap, cap);
		array->cap = cap;
	}

	array->data[array->len++] = value;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->obj.klass = vm.stringClass;
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push(OBJ_VAL(string));

	tableSet(&vm.strings, string, NIL_VAL);

	pop();

	return string;
}

static uint32_t hashString(const char* key, int length) {
	uint32_t hash = 2166136261u;

	for (int i = 0; i < length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

/**
 * Takes string dynamically allocated on the heap.
 */
ObjString* takeString(char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL) {
		FREE_ARRAY(char, chars, length + 1);
		return interned;
	}

	return allocateString(chars, length, hash);
}

/**
 * Copies static C string.
 */
ObjString* copyString(const char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL) return interned;

	char* heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(heapChars, length, hash);
}

void printObject(Value value) {

	switch (OBJ_TYPE(value)) {

	case OBJ_ARRAY: {
		printf("[");
		for (uint32_t i = 0; i < AS_ARRAY(value)->len; i++) {
			if (i > 0) printf(", ");

			ObjArray* array = AS_ARRAY(value);
			Value val = array->data[i];

			printValue(val);
		}
		printf("]");
		break;
	}

	case OBJ_BOUND_METHOD: {
		printf("<fn %s>", AS_BOUND_METHOD(value)->method->function->name->chars);
		break;
	}

	case OBJ_CLASS: {
		printf("%s", AS_CLASS(value)->name->chars);
		break;
	}

	case OBJ_CLOSURE: {
		if (AS_CLOSURE(value)->function->name == NULL) {
			printf("<anonymous fn>");
		} else {
			printf("<fn %s>", AS_CLOSURE(value)->function->name->chars);
		}
		break;
	}

	case OBJ_FUNCTION: {
		if (AS_FUNCTION(value)->name == NULL) {
			printf("<anonymous fn>");
		} else {
			printf("<fn %s>", AS_FUNCTION(value)->name->chars);
		}

		break;
	}

	case OBJ_INSTANCE: {
		printf("%s instance", AS_INSTANCE(value)->obj.klass->name->chars);
		break;
	}

	case OBJ_NATIVE: {
		printf("<native fn>");
		break;
	}

	case OBJ_STRING: {
		printf("%s", AS_CSTRING(value));
		break;
	}

	case OBJ_UPVALUE: {
		printf("upvalue");
		break;
	}
	}
}
