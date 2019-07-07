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

	ADD_METHOD(&objectClass, "toString", 8, Object_toString, 0);
	ADD_METHOD(&objectClass, "isNil", 5, Object_isNil, 0);
	ADD_METHOD(&objectClass, "isFalsey", 8, Object_isFalsey, 0);
	ADD_METHOD(&objectClass, "isTruthy", 8, Object_isTruthy, 0);
	ADD_METHOD(&objectClass, "getClass", 8, Object_getClass, 0);

	ADD_METHOD(&vm.numberClass, "toString", 8, Number_toString, 0);

	// Value name = OBJ_VAL(copyString("toString", 8));
	// push(name);
	// Value method = OBJ_VAL(newNative(Number_toString, 0));
	// push(method);

	// tableSet(&vm.numberClass->methods, AS_STRING(name), method);

	// pop();
	// pop();
}
