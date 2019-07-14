#include "debug.h"
#include "blu.h"

static uint32_t constantInstruction(const char* name, bluChunk* chunk, uint32_t offset) {
	uint8_t constant = chunk->code.data[offset + 1];
	printf("%-16s %6d '", name, constant);
	bluPrintValue(chunk->constants.data[constant]);
	printf("'\n");
	return offset + 2;
}

static uint32_t simpleInstruction(const char* name, uint32_t offset) {
	printf("%s\n", name);
	return offset + 1;
}

uint32_t bluDisassembleInstruction(bluChunk* chunk, size_t offset) {
	printf("%04lu ", offset);
	if (offset > 0 && chunk->lines.data[offset] == chunk->lines.data[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4d ", chunk->lines.data[offset]);
	}

	uint8_t instruction = chunk->code.data[offset];

	switch (instruction) {

	case OP_CONSTANT: return constantInstruction("OP_CONSTANT", chunk, offset);
	case OP_POP: return simpleInstruction("OP_POP", offset);

	case OP_ADD: return simpleInstruction("OP_ADD", offset);
	case OP_DIVIDE: return simpleInstruction("OP_DIVIDE", offset);
	case OP_REMINDER: return simpleInstruction("OP_REMINDER", offset);
	case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
	case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);

	case OP_RETURN: return simpleInstruction("OP_RETURN", offset);

	default: printf("Unknown opcode %d\n", instruction); return offset + 1;
	}
}

void bluDisassembleChunk(bluChunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	uint32_t offset = 0;
	while (offset < chunk->code.count) {
		offset = bluDisassembleInstruction(chunk, offset);
	}
}
