#ifndef blu_vm_h
#define blu_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots;
} CallFrame;

typedef struct {
	Chunk* chunk;

	Value stack[STACK_MAX];
	Value* stackTop;

	CallFrame frames[FRAMES_MAX];
	int frameCount;

	ObjClass* numberClass;
	ObjClass* booleanClass;
	ObjClass* nilClass;

	Table globals;
	Table strings;

	ObjUpvalue* openUpvalues;

	ObjString* initString;

	size_t bytesAllocated;
	size_t nextGC;
	double timeGC;

	int grayCount;
	int grayCapacity;
	Obj** grayStack;

	Obj* objects;
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
	INTERPRET_ASSERTION_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();

InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
Value peek(int distance);

bool isFalsey(Value value);
ObjClass* getClass(Value value);

#endif
