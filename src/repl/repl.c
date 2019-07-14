#include "blu.h"
#include "repl.h"

int main() {
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluVM* vm = bluNew();

		bluInterpret(vm, line, "REPL");

		bluFree(vm);
	}
}
