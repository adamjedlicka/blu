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

static bluValue clockNative(bluVM* vm, int argCount, bluValue* args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static bluValue printNative(bluVM* vm, int argCount, bluValue* args) {
	bluPrintValue(vm, args[0]);

	return NIL_VAL;
}

static bluValue printlnNative(bluVM* vm, int argCount, bluValue* args) {
	bluPrintValue(vm, args[0]);
	printf("\n");

	return NIL_VAL;
}

static void resetStack(bluVM* vm) {
	vm->stackTop = vm->stack;
	vm->frameCount = 0;
	vm->openUpvalues = NULL;
}

static void runtimeError(bluVM* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm->frameCount - 1; i >= 0; i--) {
		bluCallFrame* frame = &vm->frames[i];
		bluObjFunction* function = frame->closure->function;

		// -1 because the IP is sitting on the next instruction to be executed.
		size_t instruction = frame->ip - function->chunk.code - 1;

		fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		} else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack(vm);
}

static void defineNative(bluVM* vm, const char* name, bluNativeFn function, int arity) {
	bluPush(vm, OBJ_VAL(bluCopyString(vm, name, (int)strlen(name))));
	bluPush(vm, OBJ_VAL(bluNewNative(vm, function, arity)));
	bluTableSet(vm, &vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
	bluPop(vm);
	bluPop(vm);
}

void bluInitVM(bluVM* vm) {
	resetStack(vm);
	vm->objects = NULL;

	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;
	vm->timeGC = 0;

	vm->grayCount = 0;
	vm->grayCapacity = 0;
	vm->grayStack = NULL;

	bluInitTable(vm, &vm->globals);
	bluInitTable(vm, &vm->strings);

	vm->initString = bluCopyString(vm, "__init", 6);

	defineNative(vm, "clock", clockNative, 0);
	defineNative(vm, "print", printNative, 1);
	defineNative(vm, "println", printlnNative, 1);

	initCore();
}

void bluFreeVM(bluVM* vm) {
	bluFreeTable(vm, &vm->globals);
	bluFreeTable(vm, &vm->strings);
	bluFreeObjects(vm);

#ifdef DEBUG
	printf("Time in GC: %f\n", vm->timeGC);
#endif
}

void bluPush(bluVM* vm, bluValue value) {
	*vm->stackTop = value;
	vm->stackTop++;
}

bluValue bluPop(bluVM* vm) {
	vm->stackTop--;
	return *vm->stackTop;
}

bluValue bluPeek(bluVM* vm, int distance) {
	return vm->stackTop[-1 - distance];
}

bool bluIsFalsey(bluVM* vm, bluValue value) {
	return IS_NIL(value) || (IS_BOOL(value) && AS_BOOL(value) == false) ||
		   (IS_NUMBER(value) && AS_NUMBER(value) == 0) || (IS_STRING(value) && AS_STRING(value)->length == 0) ||
		   (IS_ARRAY(value) && AS_ARRAY(value)->len == 0);
}

static void concatenate(bluVM* vm) {
	bluObjString* b = AS_STRING(bluPeek(vm, 0));
	bluObjString* a = AS_STRING(bluPeek(vm, 1));

	int length = a->length + b->length;
	char* chars = ALLOCATE(vm, char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	bluObjString* result = bluTakeString(vm, chars, length);

	bluPop(vm);
	bluPop(vm);

	bluPush(vm, OBJ_VAL(result));
}

static bool call(bluVM* vm, bluObjClosure* closure, int argCount) {
	if (argCount != closure->function->arity) {
		runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount);
		return false;
	}

	if (vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}

	bluCallFrame* frame = &vm->frames[vm->frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;

	// +1 to include either the called function or the receiver.
	frame->slots = vm->stackTop - (argCount + 1);
	return true;
}

static bool callbluValue(bluVM* vm, bluValue callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

		case OBJ_BOUND_METHOD: {
			bluObjBoundMethod* bound = AS_BOUND_METHOD(callee);

			// Replace the bound method with the receiver so it's in the right slot when the method is called.
			vm->stackTop[-argCount - 1] = bound->receiver;
			return call(vm, bound->method, argCount);
		}

		case OBJ_CLASS: {
			bluObjClass* klass = AS_CLASS(callee);

			// Create the instance.
			vm->stackTop[-argCount - 1] = OBJ_VAL(bluNewInstance(vm, klass));
			// Call the initializer, if there is one.
			bluValue initializer;
			if (bluTableGet(vm, &klass->methods, vm->initString, &initializer)) {
				return call(vm, AS_CLOSURE(initializer), argCount);
			} else if (argCount != 0) {
				runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
				return false;
			}

			return true;
		}

		case OBJ_CLOSURE: {
			return call(vm, AS_CLOSURE(callee), argCount);
		}

		case OBJ_NATIVE: {
			bluObjNative* native = AS_NATIVE(callee);

			if (argCount != native->arity) {
				runtimeError(vm, "Expected %d arguments but got %d.", native->arity, argCount);
				return false;
			}

			bluValue result = native->function(vm, argCount, vm->stackTop - argCount);
			vm->stackTop -= argCount + 1;
			bluPush(vm, result);
			return true;
		}

		default:
			// Do nothing.
			break;
		}
	}

	runtimeError(vm, "Can only call functions and classes.");
	return false;
}

static bool invokeFromClass(bluVM* vm, bluObjClass* klass, bluObjString* name, int argCount) {
	// Look for the method.
	bluValue method;

	if (!bluTableGet(vm, &klass->methods, name, &method)) {
		if (klass->superclass != NULL && invokeFromClass(vm, klass->superclass, name, argCount)) {
			return true;
		} else {
			runtimeError(vm, "Undefined property '%s'.", name->chars);
			return false;
		}
	}

	if (IS_NATIVE(method)) {
		return callbluValue(vm, method, argCount);
	}

	return call(vm, AS_CLOSURE(method), argCount);
}

bluObjClass* bluGetClass(bluVM* vm, bluValue value) {
	switch (value.type) {
	case VAL_BOOL: return vm->booleanClass; break;
	case VAL_NIL: return vm->nilClass; break;
	case VAL_NUMBER: return vm->numberClass; break;
	case VAL_OBJ: return AS_OBJ(value)->klass; break;
	}

	__builtin_unreachable();
}

static bool invoke(bluVM* vm, bluObjString* name, int argCount) {
	bluValue receiver = bluPeek(vm, argCount);

	if (!IS_INSTANCE(receiver)) {
		return invokeFromClass(vm, bluGetClass(vm, receiver), name, argCount);
	}

	bluObjInstance* instance = AS_INSTANCE(receiver);

	// First look for a field which may shadow a method.
	bluValue value;
	if (bluTableGet(vm, &instance->fields, name, &value)) {
		vm->stackTop[-argCount] = value;
		return callbluValue(vm, value, argCount);
	}

	return invokeFromClass(vm, instance->obj.klass, name, argCount);
}

static bool bindMethod(bluVM* vm, bluObjClass* klass, bluObjString* name) {
	bluValue method;
	if (!bluTableGet(vm, &klass->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}

	bluObjBoundMethod* bound = bluNewBoundMethod(vm, bluPeek(vm, 0), AS_CLOSURE(method));
	bluPop(vm); // Instance.
	bluPush(vm, OBJ_VAL(bound));

	return true;
}

// Captures the local variable [local] into an [Upvalue]. If that local is already in an upvalue, the existing one is
// used. (This is important to ensure that multiple closures closing over the same variable actually see the same
// variable.) Otherwise, it creates a new open upvalue and adds it to the VM's list of upvalues.
static bluObjUpvalue* captureUpvalue(bluVM* vm, bluValue* local) {
	// If there are no open upvalues at all, we must need a new one.
	if (vm->openUpvalues == NULL) {
		vm->openUpvalues = bluNewUpvalue(vm, local);
		return vm->openUpvalues;
	}

	bluObjUpvalue* prevUpvalue = NULL;
	bluObjUpvalue* upvalue = vm->openUpvalues;

	// Walk towards the bottom of the stack until we find a previously existing upvalue or reach where it should be.
	while (upvalue != NULL && upvalue->value > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	// If we found it, reuse it.
	if (upvalue != NULL && upvalue->value == local) return upvalue;

	// We walked past the local on the stack, so there must not be an upvalue for it already. Make a new one and link it
	// in in the right place to keep the list sorted.
	bluObjUpvalue* createdUpvalue = bluNewUpvalue(vm, local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		// The new one is the first one in the list.
		vm->openUpvalues = createdUpvalue;
	} else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvalues(bluVM* vm, bluValue* last) {
	while (vm->openUpvalues != NULL && vm->openUpvalues->value >= last) {
		bluObjUpvalue* upvalue = vm->openUpvalues;

		// Move the value into the upvalue itself and point the upvalue to it.
		upvalue->closed = *upvalue->value;
		upvalue->value = &upvalue->closed;

		// Pop it off the open upvalue list.
		vm->openUpvalues = upvalue->next;
	}
}

static void defineMethod(bluVM* vm, bluObjString* name) {
	bluValue method = bluPeek(vm, 0);
	bluObjClass* klass = AS_CLASS(bluPeek(vm, 1));
	bluTableSet(vm, &klass->methods, name, method);
	bluPop(vm);
	bluPop(vm);
}

static bluInterpretResult run(bluVM* vm) {

	bluCallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op)                                                                                       \
	do {                                                                                                               \
		if (!IS_NUMBER(bluPeek(vm, 0)) || !IS_NUMBER(bluPeek(vm, 1))) {                                                \
			runtimeError(vm, "Operands must be numbers.");                                                             \
			return INTERPRET_RUNTIME_ERROR;                                                                            \
		}                                                                                                              \
                                                                                                                       \
		double b = AS_NUMBER(bluPop(vm));                                                                              \
		double a = AS_NUMBER(bluPop(vm));                                                                              \
		bluPush(vm, valueType(a op b));                                                                                \
	} while (false)

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		for (bluValue* slot = vm->stack; slot < vm->stackTop; slot++) {
			printf("[ ");
			bluPrintValue(vm, *slot);
			printf(" ]");
		}
		printf("\n");
		bluDisassembleInstruction(vm, &frame->closure->function->chunk,
								  (int)(frame->ip - frame->closure->function->chunk.code));
#endif

		uint8_t instruction;
		switch (instruction = READ_BYTE()) {

		case OP_CONSTANT: bluPush(vm, READ_CONSTANT()); break;

		case OP_NIL: bluPush(vm, NIL_VAL); break;

		case OP_TRUE: bluPush(vm, BOOL_VAL(true)); break;

		case OP_FALSE: bluPush(vm, BOOL_VAL(false)); break;

		case OP_POP: bluPop(vm); break;

		case OP_GET_LOCAL: {
			uint8_t slot = READ_BYTE();
			bluPush(vm, frame->slots[slot]);
			break;
		}

		case OP_SET_LOCAL: {
			uint8_t slot = READ_BYTE();
			frame->slots[slot] = bluPeek(vm, 0);
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

		case OP_DEFINE_GLOBAL: {
			bluObjString* name = READ_STRING();
			bluTableSet(vm, &vm->globals, name, bluPeek(vm, 0));
			bluPop(vm);
			break;
		}

		case OP_SET_GLOBAL: {
			bluObjString* name = READ_STRING();
			if (bluTableSet(vm, &vm->globals, name, bluPeek(vm, 0))) {
				runtimeError(vm, "Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			bluPush(vm, *frame->closure->upvalues[slot]->value);
			break;
		}

		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->value = bluPeek(vm, 0);
			break;
		}

		case OP_GET_PROPERTY: {
			if (!IS_INSTANCE(bluPeek(vm, 0))) {
				runtimeError(vm, "Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluObjInstance* instance = AS_INSTANCE(bluPeek(vm, 0));
			bluObjString* name = READ_STRING();
			bluValue value;
			if (bluTableGet(vm, &instance->fields, name, &value)) {
				bluPop(vm); // Instance.
				bluPush(vm, value);
				break;
			}

			if (!bindMethod(vm, instance->obj.klass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_SET_PROPERTY: {
			if (!IS_INSTANCE(bluPeek(vm, 1))) {
				runtimeError(vm, "Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluObjInstance* instance = AS_INSTANCE(bluPeek(vm, 1));
			bluTableSet(vm, &instance->fields, READ_STRING(), bluPeek(vm, 0));
			bluValue value = bluPop(vm);
			bluPop(vm);
			bluPush(vm, value);
			break;
		}

		case OP_GET_SUPER: {
			bluObjString* name = READ_STRING();
			bluObjClass* superclass = AS_CLASS(bluPop(vm));
			if (!bindMethod(vm, superclass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_EQUAL: {
			bluValue b = bluPop(vm);
			bluValue a = bluPop(vm);
			bluPush(vm, BOOL_VAL(bluValuesEqual(vm, a, b)));
			break;
		}

		case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;

		case OP_LESS: BINARY_OP(BOOL_VAL, <); break;

		case OP_ADD: {
			if (IS_STRING(bluPeek(vm, 0)) && IS_STRING(bluPeek(vm, 1))) {
				concatenate(vm);
			} else if (IS_NUMBER(bluPeek(vm, 0)) && IS_NUMBER(bluPeek(vm, 1))) {
				double b = AS_NUMBER(bluPop(vm));
				double a = AS_NUMBER(bluPop(vm));
				bluPush(vm, NUMBER_VAL(a + b));
			} else if (IS_NUMBER(bluPeek(vm, 0)) && IS_STRING(bluPeek(vm, 1))) {
				char output[14];

				snprintf(output, 14, "%g", AS_NUMBER(bluPop(vm)));
				bluPush(vm, OBJ_VAL(bluCopyString(vm, output, strlen(output))));

				concatenate(vm);
			} else if (IS_ARRAY(bluPeek(vm, 1)) && IS_ARRAY(bluPeek(vm, 0))) {
				bluObjArray* arrRight = AS_ARRAY(bluPop(vm));
				bluObjArray* arrLeft = AS_ARRAY(bluPop(vm));

				bluObjArray* arr = bluNewArray(vm, arrLeft->len + arrRight->len);

				for (uint32_t i = 0; i < arrLeft->len; i++) {
					arr->data[i] = arrLeft->data[i];
				}

				for (uint32_t i = 0; i < arrRight->len; i++) {
					arr->data[arrLeft->len + i] = arrRight->data[i];
				}

				bluPush(vm, OBJ_VAL(arr));
			} else if (IS_ARRAY(bluPeek(vm, 1))) {
				bluValue value = bluPop(vm);
				bluObjArray* arr = AS_ARRAY(bluPop(vm));

				bluObjArray* array = bluNewArray(vm, arr->len);
				for (uint32_t i = 0; i < arr->len; i++) {
					array->data[i] = arr->data[i];
				}

				bluArrayPush(vm, array, value);

				bluPush(vm, OBJ_VAL(array));
			} else {
				runtimeError(vm, "First operand must be string or both operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;

		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;

		case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;

		case OP_REMINDER: {
			if (!IS_NUMBER(bluPeek(vm, 0)) || !IS_NUMBER(bluPeek(vm, 1))) {
				runtimeError(vm, "Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double b = AS_NUMBER(bluPop(vm));
			double a = AS_NUMBER(bluPop(vm));
			bluPush(vm, NUMBER_VAL((int)a % (int)b));
			break;
		}

		case OP_NOT: bluPush(vm, BOOL_VAL(bluIsFalsey(vm, bluPop(vm)))); break;

		case OP_NEGATE: {
			if (!IS_NUMBER(bluPeek(vm, 0))) {
				runtimeError(vm, "Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluPush(vm, NUMBER_VAL(-AS_NUMBER(bluPop(vm))));
			break;
		}

		case OP_ASSERT: {
			if (bluIsFalsey(vm, bluPop(vm))) {
				runtimeError(vm, "Assertion error.");
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

			if (bluIsFalsey(vm, bluPeek(vm, 0))) frame->ip += offset;

			break;
		}

		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}

		case OP_ARRAY: {
			uint8_t len = READ_BYTE();

			bluObjArray* array = bluNewArray(vm, len);

			for (uint8_t i = 0; i < len; i++) {
				bluValue val = bluPop(vm);
				array->data[len - 1 - i] = val;
			}

			bluPush(vm, OBJ_VAL(array));

			break;
		}

		case OP_ARRAY_GET: {
			bluValue index = bluPop(vm);

			if (!IS_NUMBER(index)) {
				runtimeError(vm, "Array index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (AS_NUMBER(index) != (int)AS_NUMBER(index)) {
				runtimeError(vm, "Array index must be natural number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue array = bluPop(vm);

			if (!IS_ARRAY(array)) {
				runtimeError(vm, "Only arrays can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (AS_NUMBER(index) >= AS_ARRAY(array)->len || AS_NUMBER(index) < 0) {
				runtimeError(vm, "Array index out of range.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluPush(vm, AS_ARRAY(array)->data[(int)AS_NUMBER(index)]);

			break;
		}

		case OP_ARRAY_SET: {
			bluValue value = bluPop(vm);
			bluValue index = bluPop(vm);

			if (!IS_NUMBER(index)) {
				runtimeError(vm, "Array index must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue array = bluPeek(vm, 0);

			if (!IS_ARRAY(array)) {
				runtimeError(vm, "Only arrays can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			AS_ARRAY(array)->data[(int)AS_NUMBER(index)] = value;

			break;
		}

		case OP_ARRAY_PUSH: {
			bluValue value = bluPop(vm);
			bluValue array = bluPeek(vm, 0);

			if (!IS_ARRAY(array)) {
				runtimeError(vm, "Can only push to arrays.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluArrayPush(vm, AS_ARRAY(array), value);

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
			if (!callbluValue(vm, bluPeek(vm, argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm->frames[vm->frameCount - 1];
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
			bluObjString* method = READ_STRING();
			int argCount = instruction - OP_INVOKE_0;
			if (!invoke(vm, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm->frames[vm->frameCount - 1];
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
			bluObjString* method = READ_STRING();
			int argCount = instruction - OP_SUPER_0;
			bluObjClass* superclass = AS_CLASS(bluPop(vm));
			if (!invokeFromClass(vm, superclass, method, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm->frames[vm->frameCount - 1];
			break;
		}

		case OP_CLOSURE: {
			bluObjFunction* function = AS_FUNCTION(READ_CONSTANT());

			// Create the closure and push it on the stack before creating upvalues so that it doesn't get collected.
			bluObjClosure* closure = bluNewClosure(vm, function);
			bluPush(vm, OBJ_VAL(closure));

			// Capture upvalues.
			for (int i = 0; i < closure->upvalueCount; i++) {
				uint8_t isLocal = READ_BYTE();
				uint8_t index = READ_BYTE();
				if (isLocal) {
					// Make an new upvalue to close over the parent's local variable.
					closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
				} else {
					// Use the same upvalue as the current call frame.
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}

			break;
		}

		case OP_CLOSE_UPVALUE:
			closeUpvalues(vm, vm->stackTop - 1);
			bluPop(vm);
			break;

		case OP_CLASS: {
			bluPush(vm, OBJ_VAL(bluNewClass(vm, READ_STRING())));
			break;
		}

		case OP_INHERIT: {
			bluValue superclass = bluPeek(vm, 1);
			if (!IS_CLASS(superclass)) {
				runtimeError(vm, "Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluObjClass* subclass = AS_CLASS(bluPeek(vm, 0));
			subclass->superclass = AS_CLASS(superclass);
			bluPop(vm); // Subclass.
			break;
		}

		case OP_METHOD: {
			defineMethod(vm, READ_STRING());
			break;
		}

		case OP_RETURN: {
			bluValue result = bluPop(vm);

			closeUpvalues(vm, frame->slots);

			vm->frameCount--;
			if (vm->frameCount == 0) {
#if DEBUG
				if (vm->stackTop - vm->stack != 0) {
					runtimeError(vm, "Stack not empty!");
					return INTERPRET_ASSERTION_ERROR;
				}
#endif
				return INTERPRET_OK;
			}

			vm->stackTop = frame->slots;
			bluPush(vm, result);

			frame = &vm->frames[vm->frameCount - 1];
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

bluInterpretResult bluInterpret(bluVM* vm, const char* source) {
	bluObjFunction* function = bluCompile(vm, source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	bluPush(vm, OBJ_VAL(function));

	bluObjClosure* closure = bluNewClosure(vm, function);

	bluPop(vm);

	callbluValue(vm, OBJ_VAL(closure), 0);

	bluInterpretResult result = run(vm);

	return result;
}
