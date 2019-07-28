#include "object.h"
#include "include/blu.h"
#include "vm/memory.h"
#include "vm/table.h"
#include "vm/vm.h"

DEFINE_BUFFER(bluObjUpvalue, bluObjUpvalue*);

static bluObj* allocateObject(bluVM* vm, size_t size, bluObjType type) {
	bluObj* object = (bluObj*)bluAllocate(vm, size);
	object->type = type;
	object->class = NULL;
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

bluObjArray* bluNewArray(bluVM* vm, int32_t len) {
	bluObjArray* array = (bluObjArray*)allocateObject(vm, sizeof(bluObjArray), OBJ_ARRAY);
	array->cap = bluPowerOf2Ceil(len);
	array->len = len;
	array->data = bluAllocate(vm, (sizeof(bluValue) * array->cap));

	return array;
}

bluObjBoundMethod* bluNewBoundMethod(bluVM* vm, bluValue receiver, bluObjFunction* function) {
	bluObjBoundMethod* method = (bluObjBoundMethod*)allocateObject(vm, sizeof(bluObjBoundMethod), OBJ_BOUND_METHOD);
	method->receiver = receiver;
	method->function = function;

	return method;
}

bluObjClass* bluNewClass(bluVM* vm, bluObjString* name) {
	bluObjClass* class = (bluObjClass*)allocateObject(vm, sizeof(bluObjClass), OBJ_CLASS);
	class->superclass = NULL;
	class->name = name;

	bluTableInit(vm, &class->methods);

	return class;
}

bluObjFunction* bluNewFunction(bluVM* vm) {
	bluObjFunction* function = (bluObjFunction*)allocateObject(vm, sizeof(bluObjFunction), OBJ_FUNCTION);
	function->arity = 0;
	function->name = NULL;

	bluChunkInit(&function->chunk);
	bluObjUpvalueBufferInit(&function->upvalues);

	return function;
}

bluObjInstance* newInstance(bluVM* vm, bluObjClass* class) {
	bluObjInstance* instance = (bluObjInstance*)allocateObject(vm, sizeof(bluObjInstance), OBJ_INSTANCE);
	instance->obj.class = class;

	bluTableInit(vm, &instance->fields);

	return instance;
}

bluObjUpvalue* newUpvalue(bluVM* vm, bluValue* slot) {
	bluObjUpvalue* upvalue = (bluObjUpvalue*)allocateObject(vm, sizeof(bluObjUpvalue), OBJ_UPVALUE);
	upvalue->closed = NIL_VAL;
	upvalue->value = slot;
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

void bluArrayPush(bluVM* vm, bluObjArray* array, bluValue value) {
	if (array->len == array->cap) {
		int32_t newCap = bluPowerOf2Ceil(array->cap * 2);
		array->data = bluReallocate(vm, array->data, (sizeof(bluValue) * array->cap), (sizeof(bluValue) * newCap));
		array->cap = newCap;
	}

	array->data[array->len++] = value;
}

void bluPrintObject(bluValue value) {

	switch (OBJ_TYPE(value)) {

	case OBJ_ARRAY: {
		bluObjArray* array = AS_ARRAY(value);

		printf("[");
		for (int32_t i = 0; i < array->len; i++) {
			if (i > 0) {
				printf(", ");
			}

			bluPrintValue(array->data[i]);
		}
		printf("]");
		break;
	}

	case OBJ_BOUND_METHOD: {
		printf("<method %s>", AS_BOUND_METHOD(value)->function->name->chars);
		break;
	}

	case OBJ_CLASS: {
		printf("<class %s>", AS_CLASS(value)->name->chars);
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
		printf("<instance of %s>", AS_INSTANCE(value)->obj.class->name->chars);
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
