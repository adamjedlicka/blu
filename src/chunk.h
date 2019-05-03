#ifndef blu_chunk_h
#define blu_chunk_h

#include "value.h"

typedef enum {
	OP_CONSTANT,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,

	OP_POP,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_SET_GLOBAL,

	OP_JUMP,
	OP_JUMP_IF_FALSE,
	OP_LOOP,

	OP_CALL_0,
	OP_CALL_1,
	OP_CALL_2,
	OP_CALL_3,
	OP_CALL_4,
	OP_CALL_5,
	OP_CALL_6,
	OP_CALL_7,
	OP_CALL_8,

	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NOT,
	OP_NEGATE,

	OP_PRINT,
	OP_ASSERT,
	OP_RETURN,
} OpCode;

typedef struct {
	int count;
	int capacity;
	uint8_t* code;
	int* lines;
	ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);

void writeChunk(Chunk* chunk, uint8_t byte, int line);

int addConstant(Chunk* chunk, Value value);

#endif
