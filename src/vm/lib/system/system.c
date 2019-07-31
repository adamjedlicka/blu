#include <time.h>

#include "system.h"
#include "vm/memory.h"
#include "vm/object.h"
#include "vm/value.h"

#include "system.blu.inc"

int8_t System__print(bluVM* vm, int8_t argCount, bluValue* args) {
	for (int32_t i = 1; i <= argCount; i++) {
		bluPrintValue(args[i]);
	}

	return 1;
}

int8_t System__println(bluVM* vm, int8_t argCount, bluValue* args) {
	if (argCount == 0) {
		printf("\n");
	}

	for (int32_t i = 1; i <= argCount; i++) {
		bluPrintValue(args[i]);
		printf("\n");
	}

	return 1;
}

int8_t System__readline(bluVM* vm, int8_t argCount, bluValue* args) {
	char* line = bluAllocate(vm, sizeof(char) * 1024);

	if (!fgets(line, 1024, stdin)) {
		printf("\n");
	}

	bluObjString* string = bluTakeString(vm, line, strlen(line) - 1);

	args[0] = OBJ_VAL(string);

	return 1;
}

int8_t System__clock(bluVM* vm, int8_t argCount, bluValue* args) {
	args[0] = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);

	return 1;
}

void bluInitSystem(bluVM* vm) {
	bluInterpret(vm, systemSource, "__SYSTEM__");

	bluObj* systemClass = bluGetGlobal(vm, "System");
	bluDefineStaticMethod(vm, systemClass, "print", System__print, 0);
	bluDefineStaticMethod(vm, systemClass, "println", System__println, 0);
	bluDefineStaticMethod(vm, systemClass, "readline", System__readline, 0);
	bluDefineStaticMethod(vm, systemClass, "clock", System__clock, 0);
}
