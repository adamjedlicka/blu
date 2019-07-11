#ifndef blu_vm_h
#define blu_vm_h

#include "blu.h"
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
	bluObjClosure* closure;
	uint8_t* ip;
	bluValue* slots;
} bluCallFrame;

struct _bluVM {
	bluChunk* chunk;

	bluValue stack[STACK_MAX];
	bluValue* stackTop;

	bluCallFrame frames[FRAMES_MAX];
	int frameCount;

	bluObjClass* numberClass;
	bluObjClass* booleanClass;
	bluObjClass* nilClass;
	bluObjClass* stringClass;
	bluObjClass* arrayClass;
	bluObjClass* classClass;
	bluObjClass* functionClass;

	bluTable globals;
	bluTable strings;

	bluObjUpvalue* openUpvalues;

	bluObjString* initString;

	size_t bytesAllocated;
	size_t nextGC;
	double timeGC;

	int grayCount;
	int grayCapacity;
	bluObj** grayStack;

	bluObj* objects;
};

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
	INTERPRET_ASSERTION_ERROR,
} bluInterpretResult;

void bluInitVM(bluVM* vm);
void bluFreeVM(bluVM* vm);

bluInterpretResult bluInterpret(bluVM* vm, const char* source);
void bluPush(bluVM* vm, bluValue value);
bluValue bluPop(bluVM* vm);
bluValue bluPeek(bluVM* vm, int distance);

bool bluIsFalsey(bluVM* vm, bluValue value);
bluObjClass* bluGetClass(bluVM* vm, bluValue value);

#endif
