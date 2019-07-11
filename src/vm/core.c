#include <stdio.h>
#include <string.h>

#include "core.h"
#include "vm.h"

#define ADD_METHOD(vm, klass, name, len, method, arity)                                                                \
	do {                                                                                                               \
		bluValue _name = OBJ_VAL(bluCopyString(vm, name, len));                                                        \
		bluPush(vm, _name);                                                                                            \
		bluValue _method = OBJ_VAL(bluNewNative(vm, method, arity));                                                   \
		bluPush(vm, _method);                                                                                          \
		bluTableSet(vm, klass->methods, AS_STRING(_name), _method);                                                    \
		bluPop(vm);                                                                                                    \
		bluPop(vm);                                                                                                    \
	} while (false);

static bluValue Object_toString(bluVM* vm, int argCount, bluValue* args) {
	bluValue receiver = bluPeek(vm, argCount);
	bluObjClass* klass = bluGetClass(vm, receiver);

	return OBJ_VAL(klass->name);
}

static bluValue Object_isNil(bluVM* vm, int argCount, bluValue* args) {
	return BOOL_VAL(false);
}

static bluValue Object_isFalsey(bluVM* vm, int argCount, bluValue* args) {
	bluValue receiver = bluPeek(vm, argCount);

	return BOOL_VAL(bluIsFalsey(vm, receiver));
}

static bluValue Object_isTruthy(bluVM* vm, int argCount, bluValue* args) {
	bluValue receiver = bluPeek(vm, argCount);

	return BOOL_VAL(!bluIsFalsey(vm, receiver));
}

static bluValue Object_getClass(bluVM* vm, int argCount, bluValue* args) {
	bluValue receiver = bluPeek(vm, argCount);
	bluObjClass* klass = bluGetClass(vm, receiver);

	return OBJ_VAL(klass);
}

static bluValue Number_toString(bluVM* vm, int argCount, bluValue* args) {
	double number = AS_NUMBER(bluPeek(vm, argCount));

	char output[50];

	if (number == (int)number) {
		snprintf(output, 50, "%d", (int)number);
	} else {
		snprintf(output, 50, "%g", number);
	}

	return OBJ_VAL(bluCopyString(vm, output, strlen(output)));
}

static bluValue Number_floor(bluVM* vm, int argCount, bluValue* args) {
	double number = AS_NUMBER(bluPeek(vm, argCount));

	return NUMBER_VAL((int)number);
}

static bluValue Number_ceil(bluVM* vm, int argCount, bluValue* args) {
	double number = AS_NUMBER(bluPeek(vm, argCount));

	return NUMBER_VAL((int)number + 1);
}

static bluValue Array_len(bluVM* vm, int argCount, bluValue* args) {
	bluObjArray* receiver = AS_ARRAY(bluPeek(vm, argCount));

	return NUMBER_VAL(receiver->len);
}

static bluValue Array_cap(bluVM* vm, int argCount, bluValue* args) {
	bluObjArray* receiver = AS_ARRAY(bluPeek(vm, argCount));

	return NUMBER_VAL(receiver->cap);
}

static bluValue Array_push(bluVM* vm, int argCount, bluValue* args) {
	bluObjArray* receiver = AS_ARRAY(bluPeek(vm, argCount));
	bluValue value = bluPeek(vm, 0);

	bluArrayPush(vm, receiver, value);

	return OBJ_VAL(receiver);
}

void initCore(bluVM* vm) {

	bluInterpret(vm, "\
		class Object {}\
        class Number {}\
        class Boolean {\
			fn toString(): @ and \"true\" or \"false\";\
		}\
        class Nil {\
			fn toString(): \"nil\";\
			fn isNil(): true;\
		}\
		class String {}\
		class Array {}\
		class Class {}\
		class Function {}\
    ");

	bluValue value;

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Object", 6), &value);
	bluObjClass* objectClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Number", 6), &value);
	vm->numberClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Boolean", 7), &value);
	vm->booleanClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Nil", 3), &value);
	vm->nilClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "String", 6), &value);
	vm->stringClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Array", 5), &value);
	vm->arrayClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Class", 5), &value);
	vm->classClass = AS_CLASS(value);

	bluTableGet(vm, &vm->globals, bluCopyString(vm, "Function", 8), &value);
	vm->functionClass = AS_CLASS(value);

	ADD_METHOD(vm, &objectClass, "toString", 8, Object_toString, 0);
	ADD_METHOD(vm, &objectClass, "isNil", 5, Object_isNil, 0);
	ADD_METHOD(vm, &objectClass, "isFalsey", 8, Object_isFalsey, 0);
	ADD_METHOD(vm, &objectClass, "isTruthy", 8, Object_isTruthy, 0);
	ADD_METHOD(vm, &objectClass, "getClass", 8, Object_getClass, 0);

	ADD_METHOD(vm, &vm->numberClass, "toString", 8, Number_toString, 0);
	ADD_METHOD(vm, &vm->numberClass, "floor", 5, Number_floor, 0);
	ADD_METHOD(vm, &vm->numberClass, "ceil", 4, Number_ceil, 0);

	ADD_METHOD(vm, &vm->arrayClass, "len", 3, Array_len, 0);
	ADD_METHOD(vm, &vm->arrayClass, "cap", 3, Array_cap, 0);
	ADD_METHOD(vm, &vm->arrayClass, "push", 4, Array_push, 1);
}
