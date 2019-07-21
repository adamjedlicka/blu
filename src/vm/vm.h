#ifndef blu_vm_h
#define blu_vm_h

#include "blu.h"
#include "compiler/chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

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

	bluTable strings;

	bluObj* objects;
};

void bluPush(bluVM* vm, bluValue value);
bluValue bluPop(bluVM* vm);
bluValue bluPeek(bluVM* vm, int32_t distance);

bool bluIsFalsey(bluValue value);

#endif
