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

static bluObjString* allocateString(bluVM* vm, const char* chars, int32_t length, int32_t hash) {
	size_t totalSize = sizeof(bluObjString) + (sizeof(char) * (length + 1));

	bluObjString* string = (bluObjString*)allocateObject(vm, totalSize, OBJ_STRING);
	string->obj.class = vm->stringClass;
	string->length = length;
	string->hash = hash;

	strncpy(string->chars, chars, length);

	string->chars[length] = '\0';

	bluTableSet(vm, &vm->strings, string, NIL_VAL);

	return string;
}

bluObjString* bluCopyString(bluVM* vm, const char* chars, int32_t length) {
	int32_t hash = hashString(chars, length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, chars, length, hash);
	if (interned != NULL) return interned;

	return allocateString(vm, chars, length, hash);
}

bluObjArray* bluNewArray(bluVM* vm, int32_t len) {
	bluObjArray* array = (bluObjArray*)allocateObject(vm, sizeof(bluObjArray), OBJ_ARRAY);
	array->obj.class = vm->arrayClass;
	array->cap = bluPowerOf2Ceil(len);
	array->len = len;
	array->data = bluAllocate(vm, (sizeof(bluValue) * array->cap));

	return array;
}

bluObjBoundMethod* bluNewBoundMethod(bluVM* vm, bluValue receiver, bluObjClosure* closure) {
	bluObjBoundMethod* method = (bluObjBoundMethod*)allocateObject(vm, sizeof(bluObjBoundMethod), OBJ_BOUND_METHOD);
	method->obj.class = vm->functionClass;
	method->receiver = receiver;
	method->closure = closure;

	return method;
}

bluObjClass* bluNewClass(bluVM* vm, bluObjString* name) {
	bluObjClass* class = (bluObjClass*)allocateObject(vm, sizeof(bluObjClass), OBJ_CLASS);
	class->obj.class = vm->classClass;
	class->superclass = NULL;
	class->name = name;
	class->construct = NULL;
	class->destruct = NULL;

	bluTableInit(vm, &class->methods);
	bluTableInit(vm, &class->fields);

	return class;
}

bluObjClosure* newClosure(bluVM* vm, bluObjFunction* function) {
	bluObjClosure* closure = (bluObjClosure*)allocateObject(vm, sizeof(bluObjClosure), OBJ_CLOSURE);
	closure->obj.class = vm->functionClass;
	closure->function = function;

	bluObjUpvalueBufferInit(&closure->upvalues);

	return closure;
}

bluObjFunction* bluNewFunction(bluVM* vm) {
	bluObjFunction* function = (bluObjFunction*)allocateObject(vm, sizeof(bluObjFunction), OBJ_FUNCTION);
	function->obj.class = vm->functionClass;
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;

	bluChunkInit(&function->chunk);

	return function;
}

bluObjInstance* bluNewInstance(bluVM* vm, bluObjClass* class) {
	bluObjInstance* instance = (bluObjInstance*)allocateObject(vm, sizeof(bluObjInstance), OBJ_INSTANCE);
	instance->obj.class = class;
	instance->data = NULL;

	bluTableInit(vm, &instance->fields);

	return instance;
}

bluObjNative* bluNewNative(bluVM* vm, bluNativeFn function, int8_t arity) {
	bluObjNative* native = (bluObjNative*)allocateObject(vm, sizeof(bluObjNative), OBJ_NATIVE);
	native->obj.class = vm->functionClass;
	native->function = function;
	native->arity = arity;

	return native;
}

bluObjString* bluNewString(bluVM* vm, int32_t length) {
	size_t totalSize = sizeof(bluObjString) + (sizeof(char) * (length + 1));

	bluObjString* string = (bluObjString*)allocateObject(vm, totalSize, OBJ_STRING);
	string->obj.class = vm->stringClass;
	string->length = length;
	string->hash = 0;

	return string;
}

bluObjUpvalue* bluNewUpvalue(bluVM* vm, bluValue* slot) {
	bluObjUpvalue* upvalue = (bluObjUpvalue*)allocateObject(vm, sizeof(bluObjUpvalue), OBJ_UPVALUE);
	// TODO : This should not be needed.
	// upvalue->obj.class = bluGetClass(vm, *slot);
	upvalue->closed = NIL_VAL;
	upvalue->value = slot;
	upvalue->next = NULL;

	return upvalue;
}

bluObjString* bluTakeString(bluVM* vm, bluObjString* string) {
	string->hash = hashString(string->chars, string->length);

	bluObjString* interned = bluTableFindString(vm, &vm->strings, string->chars, string->length, string->hash);
	if (interned != NULL) {
		return interned;
	}

	bluTableSet(vm, &vm->strings, string, NIL_VAL);

	return string;
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
		printf("<method %s>", AS_BOUND_METHOD(value)->closure->function->name->chars);
		break;
	}

	case OBJ_CLASS: {
		printf("<class %s>", AS_CLASS(value)->name->chars);
		break;
	}

	case OBJ_CLOSURE:
		if (AS_CLOSURE(value)->function->name == NULL) {
			printf("<anonymous fn>");
		} else {
			printf("<fn %s>", AS_CLOSURE(value)->function->name->chars);
		}
		break;

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

	case OBJ_NATIVE: {
		printf("<native fn>");
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
