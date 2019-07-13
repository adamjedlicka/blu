#include "chunk.h"

DEFINE_BUFFER(bluValue, bluValue);

void bluChunkInit(bluChunk* chunk) {
	ByteBufferInit(&chunk->code);
	IntBufferInit(&chunk->lines);

	bluValueBufferInit(&chunk->constants);
}

void bluChunkFree(bluChunk* chunk) {
	ByteBufferFree(&chunk->code);
	IntBufferFree(&chunk->lines);

	bluValueBufferFree(&chunk->constants);
}
