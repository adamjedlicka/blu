#include "blu.h"

static void repl() {
	char line[1024];

	bluVM* vm = bluNew();

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluInterpret(vm, line, "REPL");
	}

	bluFree(vm);
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

static void runFile(const char* path) {
	bluVM* vm = bluNew();
	char* source = readFile(path);

	bluInterpretResult result = bluInterpret(vm, source, path);

	free(source);
	bluFree(vm);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);
	if (result == INTERPRET_ASSERTION_ERROR) exit(75);
}

static void help() {
	printf("%s %s\n", "blu", BLU_VERSION_STR);
}

int main(int argc, const char* argv[]) {
	if (argc == 1) {
		repl();
	} else if (argc == 2) {
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
			help();
		} else {
			runFile(argv[1]);
		}
	} else {
		fprintf(stderr, "Usage: blu [path]\n");
		exit(64);
	}

	return 0;
}