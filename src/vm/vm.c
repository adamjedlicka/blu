#include "vm.h"
#include "blu.h"
#include "compiler/compiler.h"
#include "memory.h"
#include "object.h"
#include "vm/debug/debug.h"

#define BINARY_OP(valueType, op)                                                                                       \
	do {                                                                                                               \
		if (!IS_NUMBER(bluPeek(vm, 0)) || !IS_NUMBER(bluPeek(vm, 1))) {                                                \
			runtimeError(vm, "Operands must be numbers.");                                                             \
			return INTERPRET_RUNTIME_ERROR;                                                                            \
		}                                                                                                              \
                                                                                                                       \
		double right = AS_NUMBER(bluPop(vm));                                                                          \
		double left = AS_NUMBER(bluPop(vm));                                                                           \
		bluPush(vm, valueType(left op right));                                                                         \
	} while (false)

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
	char* chars = bluAllocate(vm, sizeof(char) * length + 1);
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
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.data[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

	while (true) {

		if (vm->shouldGC) bluCollectGarbage(vm);

#ifdef DEBUG_VM_TRACE
		bluDisassembleInstruction(&frame->function->chunk, frame->ip - frame->function->chunk.code.data);
#endif

		uint8_t instruction = READ_BYTE();

		switch (instruction) {

		case OP_CONSTANT: {
			bluPush(vm, READ_CONSTANT());
			break;
		}

		case OP_FALSE: {
			bluPush(vm, BOOL_VAL(false));
			break;
		}

		case OP_NIL: {
			bluPush(vm, NIL_VAL);
			break;
		}

		case OP_TRUE: {
			bluPush(vm, BOOL_VAL(true));
			break;
		}

		case OP_POP: {
			bluPop(vm);
			break;
		}

		case OP_GET_LOCAL: {
			uint16_t slot = READ_SHORT();
			bluPush(vm, frame->slots[slot]);
			break;
		}

		case OP_SET_LOCAL: {
			uint16_t slot = READ_SHORT();
			frame->slots[slot] = bluPeek(vm, 0);
			break;
		}

		case OP_DEFINE_GLOBAL: {
			bluObjString* name = READ_STRING();
			bluTableSet(vm, &vm->globals, name, bluPop(vm));
			break;
		}

		case OP_GET_GLOBAL: {
			bluObjString* name = READ_STRING();
			bluValue value;

			if (!bluTableGet(vm, &vm->globals, name, &value)) {
				runtimeError(vm, "Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}

			bluPush(vm, value);
			break;
		}

		case OP_SET_GLOBAL: {
			bluObjString* name = READ_STRING();

			if (!bluTableSet(vm, &vm->globals, name, bluPeek(vm, 0))) {
				runtimeError(vm, "Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_JUMP: {
			uint16_t offset = READ_SHORT();
			frame->ip += offset;
			break;
		}

		case OP_JUMP_IF_FALSE: {
			uint16_t offset = READ_SHORT();
			if (bluIsFalsey(bluPeek(vm, 0))) frame->ip += offset;
			break;
		}

		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}

		case OP_EQUAL: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			bool equal = bluValuesEqual(left, right);

			bluPush(vm, BOOL_VAL(equal));
			break;
		}

		case OP_NOT_EQUAL: {
			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			bool notEqual = !bluValuesEqual(left, right);

			bluPush(vm, BOOL_VAL(notEqual));
			break;
		}

		case OP_GREATER: {
			BINARY_OP(BOOL_VAL, >);
			break;
		}

		case OP_GREATER_EQUAL: {
			BINARY_OP(BOOL_VAL, >=);
			break;
		}

		case OP_LESS: {
			BINARY_OP(BOOL_VAL, <);
			break;
		}

		case OP_LESS_EQUAL: {
			BINARY_OP(BOOL_VAL, <=);
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
			BINARY_OP(NUMBER_VAL, /);
			break;
		}

		case OP_REMINDER: {
			if (!IS_NUMBER(bluPeek(vm, 0)) || !IS_NUMBER(bluPeek(vm, 1))) {
				runtimeError(vm, "Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue right = bluPop(vm);
			bluValue left = bluPop(vm);

			double result = (int)AS_NUMBER(left) % (int)AS_NUMBER(right);

			bluPush(vm, NUMBER_VAL(result));
			break;
		}

		case OP_SUBTRACT: {
			BINARY_OP(NUMBER_VAL, -);
			break;
		}

		case OP_MULTIPLY: {
			BINARY_OP(NUMBER_VAL, *);
			break;
		}

		case OP_NOT: {
			bluValue value = bluPop(vm);

			bluPush(vm, BOOL_VAL(bluIsFalsey(value)));
			break;
		}

		case OP_NEGATE: {
			if (!IS_NUMBER(bluPeek(vm, 0))) {
				runtimeError(vm, "Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double number = AS_NUMBER(bluPop(vm));

			bluPush(vm, NUMBER_VAL(-number));

			break;
		}

		case OP_RETURN: {
			return INTERPRET_OK;
		}

		case OP_ASSERT: {
#ifdef DEBUG
			bluValue value = bluPop(vm);

			if (bluIsFalsey(value)) {
				runtimeError(vm, "Assertion failed.");
				return INTERPRET_ASSERTION_ERROR;
			}
#else
			bluPop(vm);
#endif

			break;
		}

		default: {
			runtimeError(vm, "Unknown opcode.");
			return INTERPRET_RUNTIME_ERROR;
			break;
		}
		}

#ifdef DEBUG_VM_TRACE
		printf("          ");
		for (bluValue* value = vm->stack; value < vm->stackTop; value++) {
			printf("[ ");
			bluPrintValue(*value);
			printf(" ]");
		}
		printf("\n");
#endif
	}
}

bluVM* bluNew() {
	bluVM* vm = malloc(sizeof(bluVM));

	resetStack(vm);

	vm->frameCount = 0;

	bluTableInit(vm, &vm->globals);
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

	bluTableFree(vm, &vm->globals);
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

	bluObjFunction* function = bluCompilerCompile(vm, &compiler, source, name);
	if (function == NULL) {
		result = INTERPRET_COMPILE_ERROR;
	} else {
		callValue(vm, OBJ_VAL(function), 0);

		result = run(vm);
	}

	return result;
}
