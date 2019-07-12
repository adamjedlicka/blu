#include <stdio.h>

#include "common.h"
#include "repl.h"
#include "vm/parser/parser.h"

void bluREPL() {
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		bluParser parser;
		bluInitParser(&parser, line);

		bluToken token;

		do {
			token = bluNextToken(&parser);

			printf("%.*s -- %d\n", token.length, token.start, token.type);

		} while (token.type != TOKEN_EOF);
	}
}
