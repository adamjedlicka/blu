#include <stdio.h>

#include "common.h"
#include "repl.h"
#include "vm/debug/debug.h"
#include "vm/vm.h"

void bluREPL() {
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluVM vm;
		bluVMInit(&vm);

		bluVMInterpret(&vm, line, "REPL");

		bluVMFree(&vm);
	}
}
