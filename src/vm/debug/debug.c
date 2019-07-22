#include "debug.h"
#include "blu.h"
#include "vm/object.h"
#include "vm/value.h"

static int32_t constantInstruction(const char* name, bluChunk* chunk, int32_t offset) {
	uint16_t slot = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
	printf("%-16s %6d '", name, slot);
	bluPrintValue(chunk->constants.data[slot]);
	printf("'\n");
	return offset + 3;
}

static int32_t simpleInstruction(const char* name, int32_t offset) {
	printf("%s\n", name);
	return offset + 1;
}

// static int32_t byteInstruction(const char* name, bluChunk* chunk, int32_t offset) {
// 	uint8_t slot = chunk->code.data[offset + 1];
// 	printf("%-16s %6d\n", name, slot);
// 	return offset + 2;
// }

static int32_t shortInstruction(const char* name, bluChunk* chunk, int32_t offset) {
	uint16_t slot = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
	printf("%-16s %6d\n", name, slot);
	return offset + 3;
}

static int32_t jumpInstruction(const char* name, bluChunk* chunk, int32_t offset) {
	uint16_t slot = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
	printf("%-16s %6d (%d)\n", name, slot, slot + offset + 3);
	return offset + 3;
}

static int32_t loopInstruction(const char* name, bluChunk* chunk, int32_t offset) {
	uint16_t slot = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
	printf("%-16s %6d (%d)\n", name, slot, offset - slot + 2);
	return offset + 3;
}

int32_t bluDisassembleInstruction(bluChunk* chunk, int32_t offset) {
	printf("%04d ", offset);
	if (offset > 0 && chunk->lines.data[offset] == chunk->lines.data[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4d ", chunk->lines.data[offset]);
	}

	uint8_t instruction = chunk->code.data[offset];

	switch (instruction) {

	case OP_CONSTANT: return constantInstruction("OP_CONSTANT", chunk, offset);
	case OP_FALSE: return simpleInstruction("OP_FALSE", offset);
	case OP_NIL: return simpleInstruction("OP_NIL", offset);
	case OP_TRUE: return simpleInstruction("OP_TRUE", offset);

	case OP_POP: return simpleInstruction("OP_POP", offset);
	case OP_GET_LOCAL: return shortInstruction("OP_GET_LOCAL", chunk, offset);
	case OP_SET_LOCAL: return shortInstruction("OP_SET_LOCAL", chunk, offset);
	case OP_DEFINE_GLOBAL: return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
	case OP_GET_GLOBAL: return constantInstruction("OP_GET_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL: return constantInstruction("OP_SET_GLOBAL", chunk, offset);
	case OP_GET_UPVALUE: return shortInstruction("OP_GET_UPVALUE", chunk, offset);
	case OP_SET_UPVALUE: return shortInstruction("OP_SET_UPVALUE", chunk, offset);

	case OP_JUMP: return jumpInstruction("OP_JUMP", chunk, offset);
	case OP_JUMP_IF_FALSE: return jumpInstruction("OP_JUMP_IF_FALSE", chunk, offset);
	case OP_JUMP_IF_TRUE: return jumpInstruction("OP_JUMP_IF_TRUE", chunk, offset);
	case OP_LOOP: return loopInstruction("OP_LOOP", chunk, offset);

	case OP_EQUAL: return simpleInstruction("OP_EQUAL", offset);
	case OP_NOT_EQUAL: return simpleInstruction("OP_NOT_EQUAL", offset);
	case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
	case OP_GREATER_EQUAL: return simpleInstruction("OP_GREATER_EQUAL", offset);
	case OP_LESS: return simpleInstruction("OP_LESS", offset);
	case OP_LESS_EQUAL: return simpleInstruction("OP_LESS_EQUAL", offset);
	case OP_ADD: return simpleInstruction("OP_ADD", offset);
	case OP_DIVIDE: return simpleInstruction("OP_DIVIDE", offset);
	case OP_REMINDER: return simpleInstruction("OP_REMINDER", offset);
	case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
	case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);
	case OP_NOT: return simpleInstruction("OP_NOT", offset);
	case OP_NEGATE: return simpleInstruction("OP_NEGATE", offset);

	case OP_CLOSURE: {
		uint16_t slot = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
		offset += 3;

		printf("%-16s %4d ", "OP_CLOSURE", slot);
		bluPrintValue(chunk->constants.data[slot]);
		printf("\n");

		bluObjFunction* function = AS_FUNCTION(chunk->constants.data[slot]);
		for (int32_t i = 0; i < function->upvalues.count; i++) {
			bool isLocal = chunk->code.data[offset];
			uint16_t index = ((chunk->code.data[offset + 1] << 8) & 0xff) | (chunk->code.data[offset + 2] & 0xff);
			offset += 3;
			printf("%04d   |                     %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
		}

		return offset;
	}

	case OP_RETURN: return simpleInstruction("OP_RETURN", offset);

	case OP_ASSERT: return simpleInstruction("OP_ASSERT", offset);

	default: printf("Unknown opcode %d\n", instruction); return offset + 1;
	}
}

void bluDisassembleChunk(bluChunk* chunk) {
	printf("========= %s\n", chunk->name);

	int32_t offset = 0;
	while (offset < chunk->code.count) {
		offset = bluDisassembleInstruction(chunk, offset);
	}

	printf("=========\n");
}
