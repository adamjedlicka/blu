#include "chunk.h"

DEFINE_BUFFER(bluValue, bluValue);

void bluChunkInit(bluChunk* chunk) {
	ByteBufferInit(&chunk->code);
	bluValueBufferInit(&chunk->constants);
}

void bluChunkFree(bluChunk* chunk) {
	ByteBufferFree(&chunk->code);
	bluValueBufferFree(&chunk->constants);
}
