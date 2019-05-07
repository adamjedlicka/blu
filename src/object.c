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

	object->next = vm.objects;
	vm.objects = object;

	return object;
}

ObjFunction* newFunction() {
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->name = NULL;

	initChunk(&function->chunk);

	return function;
}

ObjNative* newNative(NativeFn function, int arity) {
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	native->arity = arity;

	return native;
}

ObjArray* newArray(int len) {
	ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
	array->cap = len;
	array->len = len;
	array->data = ALLOCATE(Value, len);

	return array;
}

void arrayPush(ObjArray* array, Value value) {
	if (array->len == array->cap) {
		int cap = GROW_CAPACITY(array->cap);
		array->data = GROW_ARRAY(array->data, Value, array->cap, cap);
	}
	array->data[array->len++] = value;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	tableSet(&vm.strings, string, NIL_VAL);

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

ObjString* takeString(char* chars, int length) {
	uint32_t hash = hashString(chars, length);

	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL) {
		FREE_ARRAY(char, chars, length + 1);
		return interned;
	}

	return allocateString(chars, length, hash);
}

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
	case OBJ_FUNCTION: printf("<fn %s>", AS_FUNCTION(value)->name->chars); break;
	case OBJ_NATIVE: printf("<native fn>"); break;
	case OBJ_STRING: printf("%s", AS_CSTRING(value)); break;

	case OBJ_ARRAY: {
		printf("[");
		for (int i = 0; i < AS_ARRAY(value)->len; i++) {
			if (i > 0) printf(", ");

			printValue(AS_ARRAY(value)->data[i]);
		}
		printf("]");
		break;
	}
	}
}
