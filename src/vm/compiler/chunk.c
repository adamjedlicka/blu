#include "chunk.h"

DEFINE_BUFFER(bluValue, bluValue);

void bluChunkInit(bluChunk* chunk) {
	chunk->file = NULL;
	chunk->name = NULL;

	ByteBufferInit(&chunk->code, 0);
	IntBufferInit(&chunk->lines, 0);
	IntBufferInit(&chunk->columns, 0);

	bluValueBufferInit(&chunk->constants, 0);
}

void bluChunkFree(bluChunk* chunk) {
	ByteBufferFree(&chunk->code);
	IntBufferFree(&chunk->lines);
	IntBufferFree(&chunk->columns);

	bluValueBufferFree(&chunk->constants);
}

void bluChunkWrite(bluChunk* chunk, uint8_t byte, int32_t line, int32_t column) {
	ByteBufferWrite(&chunk->code, byte);
	IntBufferWrite(&chunk->lines, line);
	IntBufferWrite(&chunk->columns, column);
}
