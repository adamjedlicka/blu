#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "object.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = disassembleInstruction(chunk, offset);
	}
}

static int constantInstructionN(const char* name, int n, Chunk* chunk, int offset) {
	uint8_t constant = chunk->code[offset + 1];
	printf("%s_%-*d %4d '", name, 15 - (int)strlen(name), n, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + 2;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
	uint8_t constant = chunk->code[offset + 1];
	printf("%-16s %6d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + 2;
}

static int simpleInstruction(const char* name, int offset) {
	printf("%s\n", name);
	return offset + 1;
}

static int simpleInstructionN(const char* name, int n, int offset) {
	printf("%s_%d\n", name, n);
	return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
	uint8_t slot = chunk->code[offset + 1];
	printf("%-16s %6d\n", name, slot);
	return offset + 2;
}

static int jumpInstruction(const char* name, Chunk* chunk, int offset) {
	uint16_t slot = ((chunk->code[offset + 1] << 8) & 0xff) | (chunk->code[offset + 2] & 0xff);
	printf("%-16s %6d (%d)\n", name, slot, slot + offset + 3);
	return offset + 3;
}

int disassembleInstruction(Chunk* chunk, int offset) {
	printf("%04d ", offset);
	if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4d ", chunk->lines[offset]);
	}

	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
	case OP_CONSTANT: return constantInstruction("OP_CONSTANT", chunk, offset);
	case OP_NIL: return simpleInstruction("OP_NIL", offset);
	case OP_TRUE: return simpleInstruction("OP_TRUE", offset);
	case OP_FALSE: return simpleInstruction("OP_FALSE", offset);
	case OP_POP: return simpleInstruction("OP_POP", offset);
	case OP_GET_LOCAL: return byteInstruction("OP_GET_LOCAL", chunk, offset);
	case OP_SET_LOCAL: return byteInstruction("OP_SET_LOCAL", chunk, offset);
	case OP_GET_GLOBAL: return constantInstruction("OP_GET_GLOBAL", chunk, offset);
	case OP_DEFINE_GLOBAL: return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL: return constantInstruction("OP_SET_GLOBAL", chunk, offset);
	case OP_GET_UPVALUE: return byteInstruction("OP_GET_UPVALUE", chunk, offset);
	case OP_SET_UPVALUE: return byteInstruction("OP_SET_UPVALUE", chunk, offset);
	case OP_GET_PROPERTY: return constantInstruction("OP_GET_PROPERTY", chunk, offset);
	case OP_SET_PROPERTY: return constantInstruction("OP_SET_PROPERTY", chunk, offset);
	case OP_GET_SUPER: return constantInstruction("OP_GET_SUPER", chunk, offset);
	case OP_ARRAY: return byteInstruction("OP_ARRAY", chunk, offset);
	case OP_ARRAY_GET: return simpleInstruction("OP_ARRAY_GET", offset);
	case OP_ARRAY_SET: return simpleInstruction("OP_ARRAY_SET", offset);
	case OP_ARRAY_PUSH: return simpleInstruction("OP_ARRAY_PUSH", offset);
	case OP_EQUAL: return simpleInstruction("OP_EQUAL", offset);
	case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
	case OP_LESS: return simpleInstruction("OP_LESS", offset);
	case OP_ADD: return simpleInstruction("OP_ADD", offset);
	case OP_SUBTRACT: return simpleInstruction("OP_SUBTRACT", offset);
	case OP_MULTIPLY: return simpleInstruction("OP_MULTIPLY", offset);
	case OP_DIVIDE: return simpleInstruction("OP_DIVIDE", offset);
	case OP_NOT: return simpleInstruction("OP_NOT", offset);
	case OP_NEGATE: return simpleInstruction("OP_NEGATE", offset);
	case OP_ASSERT: return simpleInstruction("OP_ASSERT", offset);
	case OP_JUMP: return jumpInstruction("OP_JUMP", chunk, offset);
	case OP_JUMP_IF_FALSE: return jumpInstruction("OP_JUMP_IF_FALSE", chunk, offset);
	case OP_LOOP: return jumpInstruction("OP_LOOP", chunk, offset);

	case OP_CALL_0:
	case OP_CALL_1:
	case OP_CALL_2:
	case OP_CALL_3:
	case OP_CALL_4:
	case OP_CALL_5:
	case OP_CALL_6:
	case OP_CALL_7:
	case OP_CALL_8: return simpleInstructionN("OP_CALL", instruction - OP_CALL_0, offset);

	case OP_INVOKE_0:
	case OP_INVOKE_1:
	case OP_INVOKE_2:
	case OP_INVOKE_3:
	case OP_INVOKE_4:
	case OP_INVOKE_5:
	case OP_INVOKE_6:
	case OP_INVOKE_7:
	case OP_INVOKE_8: return constantInstructionN("OP_INVOKE_", instruction - OP_INVOKE_0, chunk, offset);

	case OP_SUPER_0:
	case OP_SUPER_1:
	case OP_SUPER_2:
	case OP_SUPER_3:
	case OP_SUPER_4:
	case OP_SUPER_5:
	case OP_SUPER_6:
	case OP_SUPER_7:
	case OP_SUPER_8: return constantInstructionN("OP_SUPER_", instruction - OP_SUPER_0, chunk, offset);

	case OP_CLOSURE: {
		offset++;
		uint8_t constant = chunk->code[offset++];
		printf("%-16s %4d ", "OP_CLOSURE", constant);
		printValue(chunk->constants.values[constant]);
		printf("\n");

		ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
		for (int j = 0; j < function->upvalueCount; j++) {
			int isLocal = chunk->code[offset++];
			int index = chunk->code[offset++];
			printf("%04d   |                     %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
		}

		return offset;
	}

	case OP_CLOSE_UPVALUE: return simpleInstruction("OP_CLOSE_UPVALUE", offset);
	case OP_RETURN: return simpleInstruction("OP_RETURN", offset);
	case OP_CLASS: return constantInstruction("OP_CLASS", chunk, offset);
	case OP_INHERIT: return simpleInstruction("OP_INHERIT", offset);
	case OP_METHOD: return constantInstruction("OP_METHOD", chunk, offset);

	default: printf("Unknown opcode %d\n", instruction); return offset + 1;
	}
}