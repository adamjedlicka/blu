#ifndef blu_vm_h
#define blu_vm_h

#include <stdbool.h>

#include "compiler/chunk.h"
#include "value/value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

typedef struct {
	bluChunk* chunk;
	uint8_t* ip;
} bluCallFrame;

struct bluVM {
	bluValue stack[STACK_MAX];
	bluValue* stackTop;

	bluCallFrame frames[FRAMES_MAX];
	uint32_t frameCount;
};

void bluPush(bluVM* vm, bluValue value);
bluValue bluPop(bluVM* vm);
bluValue bluPeek(bluVM* vm, uint32_t distance);

bool bluIsFalsey(bluValue value);

#endif
