#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

static void repl(bluVM* vm) {
	char line[1024];
	for (;;) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluInterpret(vm, line);
	}
}

static char* readFile(const char* path) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

static void runFile(bluVM* vm, const char* path) {
	char* source = readFile(path);
	bluInterpretResult result = bluInterpret(vm, source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);
	if (result == INTERPRET_ASSERTION_ERROR) exit(75);
}

static void help() {
	printf("%s %s\n", "blu", BLU_VERSION_STR);
}

int main(int argc, const char* argv[]) {
	bluVM vm;
	bluInitVM(&vm);

	if (argc == 1) {
		repl(&vm);
	} else if (argc == 2) {
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
			help();
		} else {
			runFile(&vm, argv[1]);
		}
	} else {
		fprintf(stderr, "Usage: blu [path]\n");
		exit(64);
	}

	bluFreeVM(&vm);

	return 0;
}
