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

int8_t Number_floor(bluVM* vm, int8_t argCount, bluValue* args) {
	double number = AS_NUMBER(args[0]);

	double floored = (double)((int)number);

	args[0] = NUMBER_VAL(floored);

	return 1;
}

int8_t Number_ceil(bluVM* vm, int8_t argCount, bluValue* args) {
	double number = AS_NUMBER(args[0]);

	double ceiled = (double)((int)number) + 1;

	args[0] = NUMBER_VAL(ceiled);

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

int8_t String_len(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjString* string = AS_STRING(args[0]);

	args[0] = NUMBER_VAL(string->length);

	return 1;
}

int8_t String_reverse(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjString* string = AS_STRING(args[0]);

	char* reversed = bluAllocate(vm, sizeof(char) * (string->length + 1));

	reversed[string->length] = '\0';
	for (int32_t i = 0; i < string->length; i++) {
		reversed[i] = string->chars[string->length - 1 - i];
	}

	args[0] = OBJ_VAL(bluTakeString(vm, reversed, string->length));

	return 1;
}

int8_t String_toNumber(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjString* string = AS_STRING(args[0]);

	double number = atof(string->chars);

	args[0] = NUMBER_VAL(number);

	return 1;
}

int8_t String_split(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjString* string = AS_STRING(args[0]);
	bluObjString* delimeter = AS_STRING(args[1]);

	bluObjArray* array = bluNewArray(vm, 0);

	char *token, *str, *tofree;
	tofree = str = strdup(string->chars);

	while ((token = strsep(&str, delimeter->chars)) != NULL) {
		bluObjString* part = bluCopyString(vm, token, strlen(token));

		if (array->len == array->cap) {
			int32_t newCap = bluPowerOf2Ceil(array->cap * 2);

			if (newCap == 0) newCap = 8;

			array->data = bluReallocate(vm, array->data, (sizeof(bluValue) * array->cap), (sizeof(bluValue) * newCap));
			array->cap = newCap;
		}

		array->data[array->len++] = OBJ_VAL(part);
	}

	free(tofree);

	args[0] = OBJ_VAL(array);

	return 1;
}

int8_t String_substring(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjString* string = AS_STRING(args[0]);
	int from = AS_NUMBER(args[1]);
	int length = AS_NUMBER(args[2]);

	char* str = bluAllocate(vm, sizeof(char) * (length + 1));
	memcpy(str, string->chars + from, length);
	str[length] = '\0';

	args[0] = OBJ_VAL(bluTakeString(vm, str, length));

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
	bluDefineMethod(vm, numberClass, "floor", Number_floor, 0);
	bluDefineMethod(vm, numberClass, "ceil", Number_ceil, 0);
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
	bluDefineMethod(vm, stringClass, "len", String_len, 0);
	bluDefineMethod(vm, stringClass, "reverse", String_reverse, 0);
	bluDefineMethod(vm, stringClass, "toNumber", String_toNumber, 0);
	bluDefineMethod(vm, stringClass, "split", String_split, 1);
	bluDefineMethod(vm, stringClass, "substring", String_substring, 2);
	vm->stringClass = (bluObjClass*)stringClass;
}
