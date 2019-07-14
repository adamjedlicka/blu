#ifndef blu_vm_h
#define blu_vm_h

#include "compiler/chunk.h"
#include "value/value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

typedef struct {
	bluChunk* chunk;
	uint8_t* ip;
} bluCallFrame;

typedef struct {
	bluValue stack[STACK_MAX];
	bluValue* stackTop;

	bluCallFrame frames[FRAMES_MAX];
	uint32_t frameCount;
} bluVM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
	INTERPRET_ASSERTION_ERROR,
} bluInterpretResult;

void bluVMInit(bluVM* vm);
void bluVMFree(bluVM* vm);

void bluPush(bluVM* vm, bluValue value);
bluValue bluPop(bluVM* vm);
bluValue bluPeek(bluVM* vm, uint32_t distance);

bool bluIsFalsey(bluValue value);

bluInterpretResult bluVMInterpret(bluVM* vm, const char* source, const char* name);

#endif
