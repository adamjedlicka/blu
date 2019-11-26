#ifndef blu_vm_h
#define blu_vm_h

#include "compiler/chunk.h"
#include "include/blu.h"
#include "vm/object.h"
#include "vm/table.h"
#include "vm/value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

DECLARE_BUFFER(bluModule, bluModule);

typedef struct {
	bluObjClosure* closure;
	uint8_t* ip;
	bluValue* slots;
} bluCallFrame;

struct bluVM {
	bluValue stack[STACK_MAX];
	bluValue* stackTop;

	bluCallFrame frames[FRAMES_MAX];
	int32_t frameCount;
	int32_t frameCountStart;

	bluTable globals;
	bluTable strings;

	bluObjUpvalue* openUpvalues;

	bluObjClass* nilClass;
	bluObjClass* boolClass;
	bluObjClass* numberClass;
	bluObjClass* arrayClass;
	bluObjClass* classClass;
	bluObjClass* functionClass;
	bluObjClass* stringClass;

	bluObjString* stringInitializer;

	// TODO : Use hashmap instead of array
	bluModuleBuffer modules;

	bluObj* objects;

	size_t bytesAllocated;
	size_t nextGC;
	bool shouldGC;
	double timeGC;
};

bool bluIsFalsey(bluValue value);
bluObjClass* bluGetClass(bluVM* vm, bluValue value);

static inline void bluPush(bluVM* vm, bluValue value) {
	*((vm->stackTop)++) = value;
}

static inline bluValue bluPop(bluVM* vm) {
	return *(--(vm->stackTop));
}

static inline bluValue bluPeek(bluVM* vm, int32_t distance) {
	return vm->stackTop[-1 - distance];
}

#endif
