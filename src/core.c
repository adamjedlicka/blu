#include <stdio.h>
#include <string.h>

#include "core.h"
#include "vm.h"

// static Value number_toString(int argCount, Value* args) {
// 	double number = AS_NUMBER(peek(argCount));

// 	char output[50];

// 	if (number == (int)number) {
// 		snprintf(output, 50, "%d", (int)number);
// 	} else {
// 		snprintf(output, 50, "%g", number);
// 	}

// 	return OBJ_VAL(copyString(output, strlen(output)));
// }

void initCore() {
	interpret("\
		class Object {\
			fn toString(): \"Object\";\
		}\
        class Number {}\
        class Boolean {}\
        class Nil {}\
    ");

	Value value;

	tableGet(&vm.globals, copyString("Number", 6), &value);
	vm.numberClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Boolean", 7), &value);
	vm.booleanClass = AS_CLASS(value);

	tableGet(&vm.globals, copyString("Nil", 3), &value);
	vm.nilClass = AS_CLASS(value);

	// Value name = OBJ_VAL(copyString("toString", 8));
	// push(name);
	// Value method = OBJ_VAL(newNative(number_toString, 0));
	// push(method);

	// tableSet(&vm.numberClass->methods, AS_STRING(name), method);

	// pop();
	// pop();
}
