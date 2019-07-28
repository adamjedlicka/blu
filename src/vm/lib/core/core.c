#include "core.h"
#include "vm/memory.h"
#include "vm/value.h"
#include "vm/vm.h"

#include "core.blu.inc"

int8_t Object_getClass(bluVM* vm, int8_t argCount, bluValue* args) {
	bluValue value = args[0];

	args[0] = OBJ_VAL(bluGetClass(vm, value));

	return 1;
}

int8_t Object_isFalsey(bluVM* vm, int8_t argCount, bluValue* args) {
	bluValue value = args[0];

	args[0] = BOOL_VAL(bluIsFalsey(value));

	return 1;
}

int8_t Object_isTruthy(bluVM* vm, int8_t argCount, bluValue* args) {
	bluValue value = args[0];

	args[0] = BOOL_VAL(!bluIsFalsey(value));

	return 1;
}

int8_t Array_push(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjArray* array = AS_ARRAY(args[0]);
	bluValue value = args[1];

	if (array->len == array->cap) {
		int32_t newCap = bluPowerOf2Ceil(array->cap * 2);

		if (newCap == 0) newCap = 8;

		array->data = bluReallocate(vm, array->data, (sizeof(bluValue) * array->cap), (sizeof(bluValue) * newCap));
		array->cap = newCap;
	}

	array->data[array->len++] = value;

	return 1;
}

int8_t Array_len(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjArray* array = AS_ARRAY(args[0]);

	args[0] = NUMBER_VAL(array->len);

	return 1;
}

void bluInitCore(bluVM* vm) {
	bluInterpret(vm, coreSource, "__CORE__");

	bluObj* objectClass = bluGetGlobal(vm, "Object");
	bluDefineMethod(vm, objectClass, "getClass", Object_getClass, 0);
	bluDefineMethod(vm, objectClass, "isFalsey", Object_isFalsey, 0);
	bluDefineMethod(vm, objectClass, "isTruthy", Object_isTruthy, 0);

	bluObj* nilClass = bluGetGlobal(vm, "Nil");
	vm->nilClass = (bluObjClass*)nilClass;

	bluObj* boolClass = bluGetGlobal(vm, "Bool");
	vm->boolClass = (bluObjClass*)boolClass;

	bluObj* numberClass = bluGetGlobal(vm, "Number");
	vm->numberClass = (bluObjClass*)numberClass;

	bluObj* arrayClass = bluGetGlobal(vm, "Array");
	bluDefineMethod(vm, arrayClass, "push", Array_push, 1);
	bluDefineMethod(vm, arrayClass, "len", Array_len, 0);
	vm->arrayClass = (bluObjClass*)arrayClass;

	bluObj* classClass = bluGetGlobal(vm, "Class");
	vm->classClass = (bluObjClass*)classClass;

	bluObj* functionClass = bluGetGlobal(vm, "Function");
	vm->functionClass = (bluObjClass*)functionClass;

	bluObj* stringClass = bluGetGlobal(vm, "String");
	vm->stringClass = (bluObjClass*)stringClass;
}
