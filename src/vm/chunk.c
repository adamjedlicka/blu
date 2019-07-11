#include <stdlib.h>

#include "blu.h"
#include "chunk.h"
#include "memory.h"

void bluInitChunk(bluVM* vm, bluChunk* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	chunk->lines = NULL;

	bluInitValueArray(vm, &chunk->constants);
}

void bluFreeChunk(bluVM* vm, bluChunk* chunk) {
	FREE_ARRAY(vm, uint8_t, chunk->code, chunk->capacity);
	FREE_ARRAY(vm, int, chunk->lines, chunk->capacity);
	bluFreeValueArray(vm, &chunk->constants);
}

void bluWriteChunk(bluVM* vm, bluChunk* chunk, uint8_t byte, int line) {
	if (chunk->capacity < chunk->count + 1) {
		int oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(vm, chunk->code, uint8_t, oldCapacity, chunk->capacity);
		chunk->lines = GROW_ARRAY(vm, chunk->lines, int, oldCapacity, chunk->capacity);
	}

	chunk->code[chunk->count] = byte;
	chunk->lines[chunk->count] = line;
	chunk->count++;
}

int bluAddConstant(bluVM* vm, bluChunk* chunk, bluValue value) {
	// Make sure the value doesn't get collected when resizing the array.
	bluPush(vm, value);

	bluWriteValueArray(vm, &chunk->constants, value);

	bluPop(vm);

	return chunk->constants.count - 1;
}
