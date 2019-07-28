#ifndef blu_vm_h
#define blu_vm_h

#include "compiler/chunk.h"
#include "include/blu.h"
#include "vm/object.h"
#include "vm/table.h"
#include "vm/value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

typedef struct {
	bluObjFunction* function;
	uint8_t* ip;
	bluValue* slots;
} bluCallFrame;

struct bluVM {
	bluValue stack[STACK_MAX];
	bluValue* stackTop;

	bluCallFrame frames[FRAMES_MAX];
	int32_t frameCount;

	bluTable globals;
	bluTable strings;

	bluObjUpvalue* openUpvalues;

	bluObjString* stringInitializer;

	bluObj* objects;

	size_t bytesAllocated;
	size_t nextGC;
	bool shouldGC;
	double timeGC;
};

inline void bluPush(bluVM* vm, bluValue value) {
	*((vm->stackTop)++) = value;
}

inline bluValue bluPop(bluVM* vm) {
	return *(--(vm->stackTop));
}

inline bluValue bluPeek(bluVM* vm, int32_t distance) {
	return vm->stackTop[-1 - distance];
}

bool bluIsFalsey(bluValue value);

#endif
