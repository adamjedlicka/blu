#include "vm.h"
#include "blu.h"
#include "compiler/compiler.h"
#include "memory.h"
#include "object.h"
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
		bluObjFunction* function = frame->function;

		// -1 because the IP is sitting on the next instruction to be executed.
		size_t instruction = frame->ip - function->chunk.code.data - 1;

		fprintf(stderr, "[line %d] in ", function->chunk.lines.data[instruction]);
		fprintf(stderr, "%s\n", function->chunk.name);
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

static bool call(bluVM* vm, bluObjFunction* function, int8_t argCount) {
	if (argCount != function->arity) {
		runtimeError(vm, "Expected %d arguments but got %d.", function->arity, argCount);
		return false;
	}

	if (vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}

	bluCallFrame* frame = &vm->frames[vm->frameCount++];
	frame->function = function;
	frame->ip = function->chunk.code.data;

	// +1 to include either the called function or the receiver.
	frame->slots = vm->stackTop - (argCount + 1);

	return true;
}

static bool callValue(bluVM* vm, bluValue callee, int8_t argCount) {
	if (!IS_OBJ(callee)) {
		runtimeError(vm, "Can only call functions and classes.");
		return false;
	}

	switch (OBJ_TYPE(callee)) {

	case OBJ_FUNCTION: {
		return call(vm, AS_FUNCTION(callee), argCount);
	}

	default: {
		runtimeError(vm, "Can only call functions and classes.");
		return false;
	}
	}
}

static bluInterpretResult run(bluVM* vm) {

	bluCallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->function->chunk.constants.data[READ_BYTE()])

	while (true) {

		if (vm->shouldGC) bluCollectGarbage(vm);

		for (bluValue* value = vm->stack; value < vm->stackTop; value++) {
			printf("[ ");
			bluPrintValue(*value);
			printf(" ]");
		}
		printf("\n");

		bluDisassembleInstruction(&frame->function->chunk, frame->ip - frame->function->chunk.code.data);

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

	vm->frameCount = 0;

	bluTableInit(vm, &vm->strings);

	vm->objects = NULL;

	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;
	vm->shouldGC = false;
	vm->timeGC = 0;

	return vm;
}

void bluFree(bluVM* vm) {
	bluCollectMemory(vm);

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

	bluObjFunction* function = bluCompilerCompile(vm, &compiler, source);
	if (function == NULL) {
		result = INTERPRET_COMPILE_ERROR;
	} else {
		bluDisassembleChunk(&function->chunk, name);

		callValue(vm, OBJ_VAL(function), 0);

		result = run(vm);
	}

	return result;
}
