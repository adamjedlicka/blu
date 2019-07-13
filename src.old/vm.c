#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "core.h"
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
	printValue(args[0]);

	return NIL_VAL;
}

static Value printlnNative(int argCount, Value* args) {
	printValue(args[0]);
	printf("\n");

	return NIL_VAL;
}

static void resetStack() {
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;

		// -1 because the IP is sitting on the next instruction to be executed.
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

static void defineNative(const char* name, NativeFn function, int arity) {
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function, arity)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void initVM() {
	resetStack();
	vm.objects = NULL;

	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;
	vm.timeGC = 0;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	initTable(&vm.globals);
	initTable(&vm.strings);

	vm.initString = copyString("__init", 6);

	defineNative("clock", clockNative, 0);
	defineNative("print", printNative, 1);
	defineNative("println", printlnNative, 1);

	initCore();
}

void freeVM() {
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeObjects();

#ifdef DEBUG
	printf("Time in GC: %f\n", vm.timeGC);
#endif
}

void push(Value value) {
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && AS_BOOL(value) == false) ||
		   (IS_NUMBER(value) && AS_NUMBER(value) == 0) || (IS_STRING(value) && AS_STRING(value)->length == 0) ||
		   (IS_ARRAY(value) && AS_ARRAY(value)->len == 0);
}

static void concatenate() {
	ObjString* b = AS_STRING(peek(0));
	ObjString* a = AS_STRING(peek(1));

	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);

	pop();
	pop();

	push(OBJ_VAL(result));
}

static bool call(ObjClosure* closure, int argCount) {
	if (argCount != closure->function->arity) {
		runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;

	// +1 to include either the called function or the receiver.
	frame->slots = vm.stackTop - (argCount + 1);
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* bound = AS_BOUND_METHOD(callee);

			// Replace the bound method with the receiver so it's in the right slot when the method is called.
			vm.stackTop[-argCount - 1] = bound->receiver;
			return call(bound->method, argCount);
		}

		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(callee);

			// Create the instance.
			vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
			// Call the initializer, if there is one.
			Value initializer;
			if (tableGet(&klass->methods, vm.initString, &initializer)) {
				return call(AS_CLOSURE(initializer), argCount);
			} else if (argCount != 0) {
				runtimeError("Expected 0 arguments but got %d.", argCount);
				return false;
			}

			return true;
		}

		case OBJ_CLOSURE: {
			return call(AS_CLOSURE(callee), argCount);
		}

		case OBJ_NATIVE: {
			ObjNative* native = AS_NATIVE(callee);

			if (argCount != native->arity) {
				runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
				return false;
			}

			Value result = native->function(argCount, vm.stackTop - argCount);
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

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
	// Look for the method.
	Value method;

	if (!tableGet(&klass->methods, name, &method)) {
		if (klass->superclass != NULL && invokeFromClass(klass->superclass, name, argCount)) {
			return true;
		} else {
			runtimeError("Undefined property '%s'.", name->chars);
			return false;
		}
	}

	if (IS_NATIVE(method)) {
		return callValue(method, argCount);
	}

	return call(AS_CLOSURE(method), argCount);
}

ObjClass* getClass(Value value) {
	switch (value.type) {
	case VAL_BOOL: return vm.booleanClass; break;
	case VAL_NIL: return vm.nilClass; break;
	case VAL_NUMBER: return vm.numberClass; break;
	case VAL_OBJ: return AS_OBJ(value)->klass; break;
	}

	__builtin_unreachable();
}

static bool invoke(ObjString* name, int argCount) {
	Value receiver = peek(argCount);

	if (!IS_INSTANCE(receiver)) {
		return invokeFromClass(getClass(receiver), name, argCount);
	}

	ObjInstance* instance = AS_INSTANCE(receiver);

	// First look for a field which may shadow a method.
	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		vm.stackTop[-argCount] = value;
		return callValue(value, argCount);
	}

	return invokeFromClass(instance->obj.klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop(); // Instance.
	push(OBJ_VAL(bound));
	return true;
}

// Captures the local variable [local] into an [Upvalue]. If that local is already in an upvalue, the existing one is
// used. (This is important to ensure that multiple closures closing over the same variable actually see the same
// variable.) Otherwise, it creates a new open upvalue and adds it to the VM's list of upvalues.
static ObjUpvalue* captureUpvalue(Value* local) {
	// If there are no open upvalues at all, we must need a new one.
	if (vm.openUpvalues == NULL) {
		vm.openUpvalues = newUpvalue(local);
		return vm.openUpvalues;
	}

	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;

	// Walk towards the bottom of the stack until we find a previously existing upvalue or reach where it should be.
	while (upvalue != NULL && upvalue->value > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	// If we found it, reuse it.
	if (upvalue != NULL && upvalue->value == local) return upvalue;

	// We walked past the local on the stack, so there must not be an upvalue for it already. Make a new one and link it
	// in in the right place to keep the list sorted.
	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		// The new one is the first one in the list.
		vm.openUpvalues = createdUpvalue;
	} else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvalues(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->value >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;

		// Move the value into the upvalue itself and point the upvalue to
		// it.
		upvalue->closed = *upvalue->value;
		upvalue->value = &upvalue->closed;

		// Pop it off the open upvalue list.
		vm.openUpvalues = upvalue->next;
	}
}

static void defineMethod(ObjString* name) {
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop();
	pop();
}

static InterpretResult run() {

	CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
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
		disassembleInstruction(&frame->closure->function->chunk,
							   (int)(frame->ip - frame->closure->function->chunk.code));
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

		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->value);
			break;
		}

		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->value = peek(0);
			break;
		}

		case OP_GET_PROPERTY: {
			if (!IS_INSTANCE(peek(0))) {
				runtimeError("Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(0));
			ObjString* name = READ_STRING();
			Value value;
			if (tableGet(&instance->fields, name, &value)) {
				pop(); // Instance.
				push(value);
				break;
			}

			if (!bindMethod(instance->obj.klass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_SET_PROPERTY: {
			if (!IS_INSTANCE(peek(1))) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(1));
			tableSet(&instance->fields, READ_STRING(), peek(0));
			Value value = pop();
			pop();
			push(value);
			break;
		}

		case OP_GET_SUPER: {
			ObjString* name = READ_STRING();
			ObjClass* superclass = AS_CLASS(pop());
			if (!bindMethod(superclass, name)) {
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

				for (uint32_t i = 0; i < arrLeft->len; i++) {
					arr->data[i] = arrLeft->data[i];
				}

				for (uint32_t i = 0; i < arrRight->len; i++) {
					arr->data[arrLeft->len + i] = arrRight->data[i];
				}

				push(OBJ_VAL(arr));
			} else if (IS_ARRAY(peek(1))) {
				Value value = pop();
				ObjArray* arr = AS_ARRAY(pop());

				ObjArray* array = newArray(arr->len);
				for (uint32_t i = 0; i < arr->len; i++) {
					array->data[i] = arr->data[i];
				}

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

		case OP_REMINDER: {
			if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
				runtimeError("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double b = AS_NUMBER(pop());
			double a = AS_NUMBER(pop());
			push(NUMBER_VAL((int)a % (int)b));
			break;
		}

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

			if (isFalsey(peek(0))) frame->ip += offset;

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
				runtimeError("Array index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (AS_NUMBER(index) != (int)AS_NUMBER(index)) {
				runtimeError("Array index must be natural number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			Value array = pop();

			if (!IS_ARRAY(array)) {
				runtimeError("Only arrays can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (AS_NUMBER(index) >= AS_ARRAY(array)->len || AS_NUMBER(index) < 0) {
				runtimeError("Array index out of range.");
				return INTERPRET_RUNTIME_ERROR;
			}

			push(AS_ARRAY(array)->data[(int)AS_NUMBER(index)]);

			break;
		}

		case OP_ARRAY_SET: {
			Value value = pop();
			Value index = pop();

			if (!IS_NUMBER(index)) {
				runtimeError("Array index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			Value array = peek(0);

			if (!IS_ARRAY(array)) {
				runtimeError("Only arrays can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			AS_ARRAY(array)->data[(int)AS_NUMBER(index)] = value;

			break;
		}

		case OP_ARRAY_PUSH: {
			Value value = pop();
			Value array = peek(0);

			if (!IS_ARRAY(array)) {
				runtimeError("Can only push to arrays.");
				return INTERPRET_RUNTIME_ERROR;
			}

			arrayPush(AS_ARRAY(array), value);

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

		case OP_INVOKE_0:
		case OP_INVOKE_1:
		case OP_INVOKE_2:
		case OP_INVOKE_3:
		case OP_INVOKE_4:
		case OP_INVOKE_5:
		case OP_INVOKE_6:
		case OP_INVOKE_7:
		case OP_INVOKE_8: {
			ObjString* method = READ_STRING();
			int argCount = instruction - OP_INVOKE_0;
			if (!invoke(method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_SUPER_0:
		case OP_SUPER_1:
		case OP_SUPER_2:
		case OP_SUPER_3:
		case OP_SUPER_4:
		case OP_SUPER_5:
		case OP_SUPER_6:
		case OP_SUPER_7:
		case OP_SUPER_8: {
			ObjString* method = READ_STRING();
			int argCount = instruction - OP_SUPER_0;
			ObjClass* superclass = AS_CLASS(pop());
			if (!invokeFromClass(superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_CLOSURE: {
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());

			// Create the closure and push it on the stack before creating upvalues so that it doesn't get collected.
			ObjClosure* closure = newClosure(function);
			push(OBJ_VAL(closure));

			// Capture upvalues.
			for (int i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint8_t index = READ_BYTE();
				if (isLocal) {
					// Make an new upvalue to close over the parent's local variable.
					closure->upvalues[i] = captureUpvalue(frame->slots + index);
				} else {
					// Use the same upvalue as the current call frame.
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}

			break;
		}

		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm.stackTop - 1);
			pop();
			break;

		case OP_CLASS: {
			push(OBJ_VAL(newClass(READ_STRING())));
			break;
		}

		case OP_INHERIT: {
			Value superclass = peek(1);
			if (!IS_CLASS(superclass)) {
				runtimeError("Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjClass* subclass = AS_CLASS(peek(0));
			subclass->superclass = AS_CLASS(superclass);
			pop(); // Subclass.
			break;
		}

		case OP_METHOD: {
			defineMethod(READ_STRING());
			break;
		}

		case OP_RETURN: {
			Value result = pop();

			closeUpvalues(frame->slots);

			vm.frameCount--;
			if (vm.frameCount == 0) {
#if DEBUG
				if (vm.stackTop - vm.stack != 0) {
					runtimeError("Stack not empty!");
					return INTERPRET_ASSERTION_ERROR;
				}
#endif
				return INTERPRET_OK;
			}

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

	push(OBJ_VAL(function));

	ObjClosure* closure = newClosure(function);

	pop();

	callValue(OBJ_VAL(closure), 0);

	InterpretResult result = run();

	return result;
}