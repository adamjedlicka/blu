#ifndef blu_vm_h
#define blu_vm_h

#include "blu.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * (UINT8_MAX + 1))

typedef struct {
	bluObjFunction* function;
	uint8_t* ip;
	uint32_t stackOffset;
} bluCallFrame;

DECLARE_BUFFER(bluCallFrame, bluCallFrame);

struct bluVM {
	bluValueBuffer stack;
	bluCallFrameBuffer frames;

	bluTable globals;
	bluTable strings;

	bluObjUpvalue* openUpvalues;

	bluObj* objects;

	size_t bytesAllocated;
	size_t nextGC;
	bool shouldGC;
	double timeGC;
};

void bluPush(bluVM* vm, bluValue value);
bluValue bluPop(bluVM* vm);
bluValue bluPeek(bluVM* vm, int32_t distance);

bool bluIsFalsey(bluValue value);

#endif
