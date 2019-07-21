#include "vm.h"
#include "blu.h"
#include "compiler/compiler.h"
#include "memory.h"
#include "vm/debug/debug.h"

static void resetStack(bluVM* vm) {
	vm->stackTop = vm->stack;
	vm->frameCount = 0;
}

static void runtimeError(bluVM* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm->frameCount - 1; i >= 0; i--) {
		bluCallFrame* frame = &vm->frames[i];
		// TODO : Functions
		// ObjFunction* function = frame->closure->function;

		// -1 because the IP is sitting on the next instruction to be executed.
		size_t instruction = frame->ip - frame->chunk->code.data - 1;

		fprintf(stderr, "[line %d] in ", frame->chunk->lines.data[instruction]);
		fprintf(stderr, "%s\n", frame->chunk->name);
	}

	resetStack(vm);
}

static void concatenate(bluVM* vm) {
	bluObjString* right = AS_STRING(bluPop(vm));
	bluObjString* left = AS_STRING(bluPop(vm));

	int32_t length = left->length + right->length;
	char* chars = malloc(sizeof(char) * length + 1);
	memcpy(chars, left->chars, left->length);
	memcpy(chars + left->length, right->chars, right->length);
	chars[length] = '\0';

	bluObjString* result = bluTakeString(vm, chars, length);

	bluPush(vm, OBJ_VAL(result));
}

static bluInterpretResult run(bluVM* vm) {

	bluCallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->chunk->constants.data[READ_BYTE()])

	while (true) {

		for (bluValue* value = vm->stack; value < vm->stackTop; value++) {
			printf("[ ");
			bluPrintValue(*value);
			printf(" ]");
		}
		printf("\n");

		bluDisassembleInstruction(frame->chunk, frame->ip - frame->chunk->code.data);

		uint8_t instruction = READ_BYTE();

		switch (instruction) {

		case OP_CONSTANT: {
			bluPush(vm, READ_CONSTANT());
			break;
		}

		case OP_POP: {
			bluPop(vm);
			break;
		}

		case OP_ADD: {
			if (IS_STRING(bluPeek(vm, 0)) && IS_STRING(bluPeek(vm, 1))) {
				concatenate(vm);
			} else if (IS_NUMBER(bluPeek(vm, 0)) && IS_NUMBER(bluPeek(vm, 1))) {
				double left = AS_NUMBER(bluPop(vm));
				double right = AS_NUMBER(bluPop(vm));

				bluPush(vm, NUMBER_VAL(left + right));
			} else {
				runtimeError(vm, "Operands must be both numbers or strings.");
			}

			break;
		}

		case OP_DIVIDE: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			double result = AS_NUMBER(left) / AS_NUMBER(right);

			bluPush(vm, NUMBER_VAL(result));
			break;
		}

		case OP_REMINDER: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			double result = (int)AS_NUMBER(left) % (int)AS_NUMBER(right);

			bluPush(vm, NUMBER_VAL(result));
			break;
		}

		case OP_SUBTRACT: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			double result = AS_NUMBER(left) - AS_NUMBER(right);

			bluPush(vm, NUMBER_VAL(result));
			break;
		}

		case OP_MULTIPLY: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			double result = AS_NUMBER(left) * AS_NUMBER(right);

			bluPush(vm, NUMBER_VAL(result));
			break;
		}

		case OP_RETURN: {
			return INTERPRET_OK;
		}

		default: {
			runtimeError(vm, "Unknown opcode.");
			return INTERPRET_RUNTIME_ERROR;
			break;
		}
		}
	}
}

bluVM* bluNew() {
	bluVM* vm = malloc(sizeof(bluVM));

	resetStack(vm);

	bluTableInit(vm, &vm->strings);

	vm->objects = NULL;

	return vm;
}

void bluFree(bluVM* vm) {
	bluCollectGarbage(vm);

	bluTableFree(vm, &vm->strings);

	free(vm);
}

void bluPush(bluVM* vm, bluValue value) {
	*((vm->stackTop)++) = value;
}

bluValue bluPop(bluVM* vm) {
	return *(--(vm->stackTop));
}

bluValue bluPeek(bluVM* vm, int32_t distance) {
	return vm->stackTop[-1 - distance];
}

bool bluIsFalsey(bluValue value);

bluInterpretResult bluInterpret(bluVM* vm, const char* source, const char* name) {
	bluInterpretResult result;

	bluCompiler compiler;
	bluChunk chunk;

	bluCompilerInit(vm, &compiler, source);
	bluChunkInit(&chunk, name);

	bool ok = bluCompilerCompile(&compiler, &chunk);
	if (ok == false) {
		result = INTERPRET_COMPILE_ERROR;
	} else {
		bluDisassembleChunk(&chunk, name);

		bluCallFrame frame;
		frame.chunk = &chunk;
		frame.ip = frame.chunk->code.data;

		vm->frames[0] = frame;
		vm->frameCount++;

		result = run(vm);
	}

	return result;
}
