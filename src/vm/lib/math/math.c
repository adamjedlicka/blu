#include "math.h"
#include "vm/value.h"

#include "math.blu.inc"

int8_t Math__floor(bluVM* vm, int8_t argCount, bluValue* args) {
	double number = AS_NUMBER(args[1]);

	args[0] = NUMBER_VAL((int)number);

	return 1;
}

int8_t Math__ceil(bluVM* vm, int8_t argCount, bluValue* args) {
	double number = AS_NUMBER(args[1]);

	args[0] = NUMBER_VAL((int)number + 1);

	return 1;
}

void bluInitMath(bluVM* vm) {
	bluInterpret(vm, mathSource, "__MATH__");

	bluObj* mathClass = bluGetGlobal(vm, "Math");
	bluDefineStaticMethod(vm, mathClass, "floor", Math__floor, 1);
	bluDefineStaticMethod(vm, mathClass, "ceil", Math__ceil, 1);
}
