#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) (type*)allocateObject(vm, sizeof(type), objectType)

static bluObj* allocateObject(bluVM* vm, size_t size, bluObjType type) {
	bluObj* object = (bluObj*)bluReallocate(vm, NULL, 0, size);
	object->type = type;
	object->isDark = false;

	object->next = vm->objects;
	vm->objects = object;

#ifdef DEBUG_TRACE_GC
	printf("%p allocate %ld for %d\n", object, size, type);
#endif

	return object;
}

bluObjArray* bluNewArray(bluVM* vm, uint32_t len) {
	// Set cap to the closest higher power of two.
	uint32_t cap = len;
	cap |= cap >> 1;
	cap |= cap >> 2;
	cap |= cap >> 4;
	cap++;

	bluObjArray* array = ALLOCATE_OBJ(bluObjArray, OBJ_ARRAY);
	array->obj.klass = vm->arrayClass;
	array->cap = 0;
	array->len = 0;

	// Push the array to stack so GC won't collect it
	bluPush(vm, OBJ_VAL(array));

	array->data = ALLOCATE(vm, bluValue, cap);
	array->cap = cap;
	array->len = len;

	bluPop(vm);

	return array;
}

bluObjBoundMethod* bluNewBoundMethod(bluVM* vm, bluValue receiver, bluObjClosure* method) {
	bluObjBoundMethod* bound = ALLOCATE_OBJ(bluObjBoundMethod, OBJ_BOUND_METHOD);
	bound->obj.klass = method->obj.klass;
	bound->receiver = receiver;
	bound->method = method;

	return bound;
}

bluObjClass* bluNewClass(bluVM* vm, bluObjString* name) {
	bluObjClass* klass = ALLOCATE_OBJ(bluObjClass, OBJ_CLASS);
	klass->obj.klass = vm->classClass;
	klass->name = name;
	klass->superclass = NULL;
	bluInitTable(vm, &klass->methods);
	return klass;
}

bluObjClosure* bluNewClosure(bluVM* vm, bluObjFunction* function) {
	// Allocate the upvalue array first so it doesn't cause the closure to get collected.
	bluObjUpvalue** upvalues = ALLOCATE(vm, bluObjUpvalue*, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	bluObjClosure* closure = ALLOCATE_OBJ(bluObjClosure, OBJ_CLOSURE);
	closure->obj.klass = function->obj.klass;
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;

	return closure;
}

bluObjFunction* bluNewFunction(bluVM* vm) {
	bluObjFunction* function = ALLOCATE_OBJ(bluObjFunction, OBJ_FUNCTION);
	function->obj.klass = vm->functionClass;
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;

	bluInitChunk(vm, &function->chunk);

	return function;
}

bluObjInstance* bluNewInstance(bluVM* vm, bluObjClass* klass) {
	bluObjInstance* instance = ALLOCATE_OBJ(bluObjInstance, OBJ_INSTANCE);
	instance->obj.klass = klass;
	bluInitTable(vm, &instance->fields);
	return instance;
}

bluObjNative* bluNewNative(bluVM* vm, bluNativeFn function, int arity) {
	bluObjNative* native = ALLOCATE_OBJ(bluObjNative, OBJ_NATIVE);
	native->obj.klass = vm->functionClass;
	native->function = function;
	native->arity = arity;

	return native;
}

bluObjUpvalue* bluNewUpvalue(bluVM* vm, bluValue* slot) {
	bluObjUpvalue* upvalue = ALLOCATE_OBJ(bluObjUpvalue, OBJ_UPVALUE);
	upvalue->obj.klass = bluGetClass(vm, *slot);
	upvalue->closed = NIL_VAL;
	upvalue->value = slot;
	upvalue->next = NULL;

	return upvalue;
}

void bluArrayPush(bluVM* vm, bluObjArray* array, bluValue value) {
	if (array->len == array->cap) {
		int cap = GROW_CAPACITY(array->cap);
		array->data = GROW_ARRAY(vm, array->data, bluValue, array->cap, cap);
		array->cap = cap;
	}

	array->data[array->len++] = value;
}

static bluObjString* allocateString(bluVM* vm, char* chars, int length, uint32_t hash) {
	bluObjString* string = ALLOCATE_OBJ(bluObjString, OBJ_STRING);
	string->obj.klass = vm->stringClass;
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	bluPush(vm, OBJ_VAL(string));

	bluTableSet(vm, &vm->strings, string, NIL_VAL);

	bluPop(vm);

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
bluObjString* bluTakeString(bluVM* vm, char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) {
		FREE_ARRAY(vm, char, chars, length + 1);
		return interned;
	}

	return allocateString(vm, chars, length, hash);
}

/**
 * Copies static C string.
 */
bluObjString* bluCopyString(bluVM* vm, const char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) return interned;

	char* heapChars = ALLOCATE(vm, char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(vm, heapChars, length, hash);
}

void bluPrintObject(bluVM* vm, bluValue value) {

	switch (OBJ_TYPE(value)) {

	case OBJ_ARRAY: {
		printf("[");
		for (uint32_t i = 0; i < AS_ARRAY(value)->len; i++) {
			if (i > 0) printf(", ");

			bluObjArray* array = AS_ARRAY(value);
			bluValue val = array->data[i];

			bluPrintValue(vm, val);
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
