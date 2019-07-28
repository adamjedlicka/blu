#include "core.h"
#include "vm/memory.h"
#include "vm/value.h"
#include "vm/vm.h"

#include "core.blu.inc"

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

void bluInitCore(bluVM* vm) {
	bluInterpret(vm, coreSource, "__CORE__");

	bluObj* arrayClass = bluGetGlobal(vm, "Array");
	bluDefineMethod(vm, arrayClass, "push", Array_push, 1);
	vm->arrayClass = (bluObjClass*)arrayClass;
}
