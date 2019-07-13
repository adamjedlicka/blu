#include <stdio.h>

#include "common.h"
#include "repl.h"
#include "vm/compiler/compiler.h"
#include "vm/debug/debug.h"

void bluREPL() {
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluCompiler compiler;
		bluCompilerInit(&compiler, line);
		bluCompilerCompile(&compiler);

		bluDisassembleChunk(&compiler.chunk, "<top>");

		bluCompilerFree(&compiler);
	}
}
