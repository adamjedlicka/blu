#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

VM vm;

static Value clockNative(int argCount, Value* args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value printNative(int argCount, Value* args) {
	for (int i = 0; i < argCount; i++) {
		printValue(args[i]);
		printf("\n");
	}

	return NIL_VAL;
}

static Value lenNative(int argCount, Value* args) {
	ObjArray* arr = AS_ARRAY(args[0]);
	return NUMBER_VAL(arr->len);
}

static void resetStack() {
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->function;
		// -1 because the IP is sitting on the next instruction to be
		// executed.
		size_t instruction = frame->ip - function->chunk.code - 1;
		fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		} else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack();
}

static void defineNative(const char* name, NativeFn function) {
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void initVM() {
	resetStack();
	vm.objects = NULL;
	initTable(&vm.globals);
	initTable(&vm.strings);

	defineNative("clock", clockNative);
	defineNative("print", printNative);
	defineNative("len", lenNative);
}

void freeVM() {
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeObjects();

	// Reset VM data
	initVM();
}

void push(Value value) {
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

static Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
	ObjString* b = AS_STRING(pop());
	ObjString* a = AS_STRING(pop());

	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);
	push(OBJ_VAL(result));
}

static bool call(ObjFunction* function, int argCount) {
	if (argCount != function->arity) {
		runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->function = function;
	frame->ip = function->chunk.code;

	// +1 to include either the called function or the receiver.
	frame->slots = vm.stackTop - (argCount + 1);
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

		case OBJ_FUNCTION: return call(AS_FUNCTION(callee), argCount);

		case OBJ_NATIVE: {
			NativeFn native = AS_NATIVE(callee);
			Value result = native(argCount, vm.stackTop - argCount);
			vm.stackTop -= argCount + 1;
			push(result);
			return true;
		}

		default:
			// Do nothing.
			break;
		}
	}

	runtimeError("Can only call functions and classes.");
	return false;
}

static InterpretResult run() {

	CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op)                                                                                       \
	do {                                                                                                               \
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                                                              \
			runtimeError("Operands must be numbers.");                                                                 \
			return INTERPRET_RUNTIME_ERROR;                                                                            \
		}                                                                                                              \
                                                                                                                       \
		double b = AS_NUMBER(pop());                                                                                   \
		double a = AS_NUMBER(pop());                                                                                   \
		push(valueType(a op b));                                                                                       \
	} while (false)

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
			printf("[ ");
			printValue(*slot);
			printf(" ]");
		}
		printf("\n");
		disassembleInstruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code));
#endif

		uint8_t instruction;
		switch (instruction = READ_BYTE()) {

		case OP_CONSTANT: push(READ_CONSTANT()); break;

		case OP_NIL: push(NIL_VAL); break;

		case OP_TRUE: push(BOOL_VAL(true)); break;

		case OP_FALSE: push(BOOL_VAL(false)); break;

		case OP_POP: pop(); break;

		case OP_GET_LOCAL: {
			uint8_t slot = READ_BYTE();
			push(frame->slots[slot]);
			break;
		}

		case OP_SET_LOCAL: {
			uint8_t slot = READ_BYTE();
			frame->slots[slot] = peek(0);
			break;
		}

		case OP_GET_GLOBAL: {
			ObjString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}

		case OP_DEFINE_GLOBAL: {
			ObjString* name = READ_STRING();
			tableSet(&vm.globals, name, peek(0));
			pop();
			break;
		}

		case OP_SET_GLOBAL: {
			ObjString* name = READ_STRING();
			if (tableSet(&vm.globals, name, peek(0))) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_EQUAL: {
			Value b = pop();
			Value a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;
		}

		case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;

		case OP_LESS: BINARY_OP(BOOL_VAL, <); break;

		case OP_ADD: {
			if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
				concatenate();
			} else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				double b = AS_NUMBER(pop());
				double a = AS_NUMBER(pop());
				push(NUMBER_VAL(a + b));
			} else if (IS_NUMBER(peek(0)) && IS_STRING(peek(1))) {
				char output[14];

				snprintf(output, 14, "%g", AS_NUMBER(pop()));
				push(OBJ_VAL(copyString(output, strlen(output))));

				concatenate();
			} else if (IS_ARRAY(peek(1)) && IS_ARRAY(peek(0))) {
				ObjArray* arrRight = AS_ARRAY(pop());
				ObjArray* arrLeft = AS_ARRAY(pop());

				ObjArray* arr = newArray(arrLeft->len + arrRight->len);

				for (int i = 0; i < arrLeft->len; i++) {
					arr->data[i] = arrLeft->data[i];
				}

				for (int i = 0; i < arrRight->len; i++) {
					arr->data[arrLeft->len + i] = arrRight->data[i];
				}

				push(OBJ_VAL(arr));
			} else if (IS_ARRAY(peek(1))) {
				Value value = pop();
				ObjArray* array = AS_ARRAY(pop());

				arrayPush(array, value);

				push(OBJ_VAL(array));
			} else {
				runtimeError("First operand must be string or both operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;

		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;

		case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;

		case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;

		case OP_NEGATE: {
			if (!IS_NUMBER(peek(0))) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			push(NUMBER_VAL(-AS_NUMBER(pop())));
			break;
		}

		case OP_ASSERT: {
			if (isFalsey(pop())) {
				runtimeError("Assertion error.");
				return INTERPRET_ASSERTION_ERROR;
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
			Value condition = pop();

			if (isFalsey(condition)) frame->ip += offset;

			break;
		}

		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}

		case OP_ARRAY: {
			uint8_t len = READ_BYTE();

			ObjArray* array = newArray(len);
			for (uint8_t i = 0; i < len; i++) {
				Value val = pop();
				array->data[len - 1 - i] = val;
			}

			push(OBJ_VAL(array));

			break;
		}

		case OP_ARRAY_GET: {
			Value index = pop();

			if (!IS_NUMBER(index)) {
				runtimeError("Array access index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			Value array = pop();

			if (!IS_ARRAY(array)) {
				runtimeError("Only arrays can be accessed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			push(AS_ARRAY(array)->data[(int)AS_NUMBER(index)]);

			break;
		}

		case OP_ARRAY_SET: {
			Value value = pop();
			Value index = pop();

			if (!IS_NUMBER(index)) {
				runtimeError("Array access index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			Value array = peek(0);

			if (!IS_ARRAY(array)) {
				runtimeError("Only arrays can be accessed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			AS_ARRAY(array)->data[(int)AS_NUMBER(index)] = value;

			break;
		}

		case OP_CALL_0:
		case OP_CALL_1:
		case OP_CALL_2:
		case OP_CALL_3:
		case OP_CALL_4:
		case OP_CALL_5:
		case OP_CALL_6:
		case OP_CALL_7:
		case OP_CALL_8: {
			int argCount = instruction - OP_CALL_0;
			if (!callValue(peek(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_RETURN: {
			Value result = pop();

			vm.frameCount--;
			if (vm.frameCount == 0) return INTERPRET_OK;

			vm.stackTop = frame->slots;
			push(result);

			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	callValue(OBJ_VAL(function), 0);

	InterpretResult result = run();

	return result;
}
