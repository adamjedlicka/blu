#include "system.h"
#include "vm/value.h"

#include "system.blu.inc"

int8_t System__print(bluVM* vm, int8_t argCount, bluValue* args) {
	bluPrintValue(args[1]);

	return 1;
}

int8_t System__println(bluVM* vm, int8_t argCount, bluValue* args) {
	bluPrintValue(args[1]);
	printf("\n");

	return 1;
}

void bluInitSystem(bluVM* vm) {
	bluInterpret(vm, systemSource, "__SYSTEM__");

	bluObj* systemClass = bluGetGlobal(vm, "System");
	bluDefineStaticMethod(vm, systemClass, "print", System__print, 1);
	bluDefineStaticMethod(vm, systemClass, "println", System__println, 1);
}
