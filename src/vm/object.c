#include "object.h"
#include "blu.h"
#include "table.h"
#include "vm.h"

static uint32_t hashString(const char* key, uint32_t length) {
	uint32_t hash = 2166136261u;

	for (uint32_t i = 0; i < length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

static bluObjString* allocateString(bluVM* vm, char* chars, uint32_t length, uint32_t hash) {
	bluObjString* string = malloc(sizeof(bluObjString));
	string->obj.type = OBJ_STRING;
	string->chars = chars;
	string->length = length;
	string->hash = hash;

	bluTableSet(vm, &vm->strings, string, NIL_VAL);

	return string;
}

bluObjString* bluTakeString(bluVM* vm, char* chars, uint32_t length) {
	uint32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) {
		free(chars);
		return interned;
	}

	return allocateString(vm, chars, length, hash);
}

bluObjString* bluCopyString(bluVM* vm, const char* chars, uint32_t length) {
	uint32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) return interned;

	char* heapChars = malloc(sizeof(char) * (length + 1));
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';

	return allocateString(vm, heapChars, length, hash);
}

void bluPrintObject(bluValue value) {

	switch (OBJ_TYPE(value)) {
	case OBJ_STRING: {
		printf("%s", AS_CSTRING(value));
		break;
	}
	}
}
