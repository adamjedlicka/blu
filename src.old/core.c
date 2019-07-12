#include <stdio.h>
#include <string.h>

#include "core.h"
#include "vm.h"

#define ADD_METHOD(klass, name, len, method, arity)                                                                    \
	do {                                                                                                               \
		Value _name = OBJ_VAL(copyString(name, len));                                                                  \
		push(_name);                                                                                                   \
		Value _method = OBJ_VAL(newNative(method, arity));                                                             \
		push(_method);                                                                                                 \
		tableSet(klass->methods, AS_STRING(_name), _method);                                                           \
		pop();                                                                                                         \
		pop();                                                                                                         \
	} while (false);

static Value Object_toString(int argCount, Value* args) {
	Value receiver = peek(argCount);
	ObjClass* klass = getClass(receiver);

	return OBJ_VAL(klass->name);
}

static Value Object_isNil(int argCount, Value* args) {
	return BOOL_VAL(false);
}

static Value Object_isFalsey(int argCount, Value* args) {
	Value receiver = peek(argCount);

	return BOOL_VAL(isFalsey(receiver));
}

static Value Object_isTruthy(int argCount, Value* args) {
	Value receiver = peek(argCount);

	return BOOL_VAL(!isFalsey(receiver));
}

static Value Object_getClass(int argCount, Value* args) {
	Value receiver = peek(argCount);
	ObjClass* klass = getClass(receiver);

	return OBJ_VAL(klass);
}

static Value Number_toString(int argCount, Value* args) {
	double number = AS_NUMBER(peek(argCount));

	char output[50];

	if (number == (int)number) {
		snprintf(output, 50, "%d", (int)number);
	} else {
		snprintf(output, 50, "%g", number);
	}

	return OBJ_VAL(copyString(output, strlen(output)));
}

static Value Number_floor(int argCount, Value* args) {
	double number = AS_NUMBER(peek(argCount));

	return NUMBER_VAL((int)number);
}

static Value Number_ceil(int argCount, Value* args) {
	double number = AS_NUMBER(peek(argCount));

	return NUMBER_VAL((int)number + 1);
}

static Value Array_len(int argCount, Value* args) {
	ObjArray* receiver = AS_ARRAY(peek(argCount));

	return NUMBER_VAL(receiver->len);
}

static Value Array_cap(int argCount, Value* args) {
	ObjArray* receiver = AS_ARRAY(peek(argCount));

	return NUMBER_VAL(receiver->cap);
}

static Value Array_push(int argCount, Value* args) {
	ObjArray* receiver = AS_ARRAY(peek(argCount));
	Value value = peek(0);

	arrayPush(receiver, value);

	return OBJ_VAL(receiver);
}

void initCore() {
	interpret("\
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

	Value value;

	tableGet(&vm.globals, copyString("Object", 6), &value);
	ObjClass* objectClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Number", 6), &value);
	vm.numberClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Boolean", 7), &value);
	vm.booleanClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Nil", 3), &value);
	vm.nilClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("String", 6), &value);
	vm.stringClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Array", 5), &value);
	vm.arrayClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Class", 5), &value);
	vm.classClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Function", 8), &value);
	vm.functionClass = AS_CLASS(value);

	ADD_METHOD(&objectClass, "toString", 8, Object_toString, 0);
	ADD_METHOD(&objectClass, "isNil", 5, Object_isNil, 0);
	ADD_METHOD(&objectClass, "isFalsey", 8, Object_isFalsey, 0);
	ADD_METHOD(&objectClass, "isTruthy", 8, Object_isTruthy, 0);
	ADD_METHOD(&objectClass, "getClass", 8, Object_getClass, 0);

	ADD_METHOD(&vm.numberClass, "toString", 8, Number_toString, 0);
	ADD_METHOD(&vm.numberClass, "floor", 5, Number_floor, 0);
	ADD_METHOD(&vm.numberClass, "ceil", 4, Number_ceil, 0);

	ADD_METHOD(&vm.arrayClass, "len", 3, Array_len, 0);
	ADD_METHOD(&vm.arrayClass, "cap", 3, Array_cap, 0);
	ADD_METHOD(&vm.arrayClass, "push", 4, Array_push, 1);
}
