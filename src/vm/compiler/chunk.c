#include "chunk.h"

DEFINE_BUFFER(bluValue, bluValue);

void bluChunkInit(bluChunk* chunk) {
	ByteBufferInit(&chunk->code);
	IntBufferInit(&chunk->lines);
	IntBufferInit(&chunk->columns);

	bluValueBufferInit(&chunk->constants);
}

void bluChunkFree(bluChunk* chunk) {
	ByteBufferFree(&chunk->code);
	IntBufferFree(&chunk->lines);
	IntBufferFree(&chunk->columns);

	bluValueBufferFree(&chunk->constants);
}

void bluChunkWrite(bluChunk* chunk, uint8_t byte, uint32_t line, uint32_t column) {
	ByteBufferWrite(&chunk->code, byte);
	IntBufferWrite(&chunk->lines, line);
	IntBufferWrite(&chunk->columns, column);
}
