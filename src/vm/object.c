#include "object.h"
#include "blu.h"
#include "memory.h"
#include "table.h"
#include "vm.h"

DEFINE_BUFFER(bluObjUpvalue, bluObjUpvalue*);

static bluObj* allocateObject(bluVM* vm, size_t size, bluObjType type) {
	bluObj* object = (bluObj*)bluAllocate(vm, size);
	object->type = type;
	object->isDark = false;
	object->next = vm->objects;

	vm->objects = object;

	return object;
}

static int32_t hashString(const char* key, int32_t length) {
	int32_t hash = 2166136261u;

	for (int32_t i = 0; i < length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

static bluObjString* allocateString(bluVM* vm, char* chars, int32_t length, int32_t hash) {
	bluObjString* string = (bluObjString*)allocateObject(vm, sizeof(bluObjString), OBJ_STRING);
	string->chars = chars;
	string->length = length;
	string->hash = hash;

	bluTableSet(vm, &vm->strings, string, NIL_VAL);

	return string;
}

bluObjString* bluCopyString(bluVM* vm, const char* chars, int32_t length) {
	int32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) return interned;

	char* heapChars = bluAllocate(vm, sizeof(char) * (length + 1));
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(vm, heapChars, length, hash);
}

bluObjFunction* bluNewFunction(bluVM* vm) {
	bluObjFunction* function = (bluObjFunction*)allocateObject(vm, sizeof(bluObjFunction), OBJ_FUNCTION);
	function->arity = 0;
	function->name = NULL;

	bluChunkInit(&function->chunk);
	bluObjUpvalueBufferInit(&function->upvalues, 8);

	return function;
}

bluObjUpvalue* newUpvalue(bluVM* vm, int32_t stackOffset) {
	bluObjUpvalue* upvalue = (bluObjUpvalue*)allocateObject(vm, sizeof(bluObjUpvalue), OBJ_UPVALUE);
	upvalue->closed = NIL_VAL;
	upvalue->stackOffset = stackOffset;
	upvalue->next = NULL;

	return upvalue;
}

bluObjString* bluTakeString(bluVM* vm, char* chars, int32_t length) {
	int32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) {
		bluDeallocate(vm, chars, sizeof(char) * 1);
		return interned;
	}

	return allocateString(vm, chars, length, hash);
}

void bluPrintObject(bluValue value) {

	switch (OBJ_TYPE(value)) {

	case OBJ_FUNCTION: {
		if (AS_FUNCTION(value)->name == NULL) {
			printf("<anonymous fn>");
		} else {
			printf("<fn %s>", AS_FUNCTION(value)->name->chars);
		}
		break;
	}

	case OBJ_UPVALUE: {
		printf("upvalue");
		break;
	}

	case OBJ_STRING: {
		printf("%s", AS_CSTRING(value));
		break;
	}
	}
}
