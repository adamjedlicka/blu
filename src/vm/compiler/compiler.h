#ifndef blu_compiler_h
#define blu_compiler_h

#include "blu.h"
#include "chunk.h"
#include "vm/parser/parser.h"

typedef struct {
	bluVM* vm;
	bluParser parser;

	bluToken previous;
	bluToken current;

	bluChunk* chunk;

	bool hadError;
	bool panicMode;
} bluCompiler;

void bluCompilerInit(bluVM* vm, bluCompiler* compiler, const char* source);
void bluCompilerFree(bluCompiler* compiler);

bool bluCompilerCompile(bluCompiler* compiler, bluChunk* chunk);

#endif
