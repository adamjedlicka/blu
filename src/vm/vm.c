#include "vm.h"
#include "compiler/compiler.h"
#include "lib/std.h"
#include "vm/debug/debug.h"
#include "vm/memory.h"
#include "vm/object.h"

#define BINARY_OP(valueType, op)                                                                                       \
	do {                                                                                                               \
		if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) {                                                              \
			RUNTIME_ERROR("Operands must be numbers.");                                                                \
			return INTERPRET_RUNTIME_ERROR;                                                                            \
		}                                                                                                              \
                                                                                                                       \
		double right = AS_NUMBER(POP());                                                                               \
		double left = AS_NUMBER(POP());                                                                                \
		PUSH(valueType(left op right));                                                                                \
	} while (false)

DEFINE_BUFFER(bluModule, bluModule);

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
		bluObjFunction* function = frame->closure->function;

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

static bool call(bluVM* vm, bluObjClosure* closure, int8_t argCount) {
	if (argCount < closure->function->arity) {
		runtimeError(vm, "Expected %d arguments but got %d.", closure->function->arity, argCount);
		return false;
	}

	if (vm->frameCount == FRAMES_MAX) {
		runtimeError(vm, "Stack overflow.");
		return false;
	}

	bluCallFrame* frame = &vm->frames[vm->frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code.data;

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

	case OBJ_BOUND_METHOD: {
		bluObjBoundMethod* boundMethod = AS_BOUND_METHOD(callee);

		// Replace the bound method with the new receiver so it's in the right slot when the method is called.
		vm->stackTop[-argCount - 1] = boundMethod->receiver;
		return call(vm, boundMethod->closure, argCount);
	}

	case OBJ_CLASS: {
		bluObjClass* class = AS_CLASS(callee);

		// Create the instance.
		vm->stackTop[-argCount - 1] = OBJ_VAL(bluNewInstance(vm, class));

		// Call the initializer, if there is one.
		bluValue initializer;
		if (bluTableGet(vm, &class->methods, vm->stringInitializer, &initializer)) {
			if (!callValue(vm, initializer, argCount)) {
				return false;
			}
		} else if (argCount != 0) {
			runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
			return false;
		}

		if (class->construct != NULL) {
			class->construct(vm, AS_INSTANCE(vm->stackTop[-argCount - 1]));
		}

		return true;
	}

	case OBJ_CLOSURE: {
		return call(vm, AS_CLOSURE(callee), argCount);
	}

	case OBJ_NATIVE: {
		bluObjNative* native = AS_NATIVE(callee);

		if (argCount < native->arity) {
			runtimeError(vm, "Expected %d arguments but got %d.", native->arity, argCount);
			return false;
		}

		int8_t result = native->function(vm, argCount, vm->stackTop - argCount - 1);
		if (result < 0) {
			runtimeError(vm, "Something went wrong.");
			return false;
		}

		vm->stackTop -= argCount + 1 - result;

		return true;
	}

	default: {
		runtimeError(vm, "Can only call functions and classes.");
		return false;
	}
	}
}

static bool invokeFromClass(bluVM* vm, bluObjClass* class, bluObjString* name, int8_t argCount) {
	// Look for the method.
	bluValue method;
	if (!bluTableGet(vm, &class->methods, name, &method)) {
		if (class->superclass != NULL && invokeFromClass(vm, class->superclass, name, argCount)) {
			return true;
		} else {
			runtimeError(vm, "Undefined property '%s'.", name->chars);
			return false;
		}
	}

	if (IS_NATIVE(method)) {
		return callValue(vm, method, argCount);
	}

	return call(vm, AS_CLOSURE(method), argCount);
}

static bool invoke(bluVM* vm, bluObjString* name, int8_t argCount) {
	bluValue receiver = bluPeek(vm, argCount);

	if (IS_INSTANCE(receiver)) {
		bluValue value;
		if (bluTableGet(vm, &AS_INSTANCE(receiver)->fields, name, &value)) {
			return callValue(vm, value, argCount);
		}
	} else if (IS_CLASS(receiver)) {
		bluValue value;
		if (bluTableGet(vm, &AS_CLASS(receiver)->fields, name, &value)) {
			return callValue(vm, value, argCount);
		}
	}

	bluObjClass* class = bluGetClass(vm, receiver);
	return invokeFromClass(vm, class, name, argCount);
}

// Captures the local variable [local] into an [Upvalue]. If that local is already in an upvalue, the existing one is
// used. (This is important to ensure that multiple closures closing over the same variable actually see the same
// variable.) Otherwise, it creates a new open upvalue and adds it to the VM's list of upvalues.
static bluObjUpvalue* captureUpvalue(bluVM* vm, bluValue* local) {
	// If there are no open upvalues at all, we must need a new one.
	if (vm->openUpvalues == NULL) {
		vm->openUpvalues = bluNewUpvalue(vm, local);
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

static bool bindMethod(bluVM* vm, bluObjClass* class, bluObjString* name) {
	bluValue method;
	if (!bluTableGet(vm, &class->methods, name, &method)) {
		runtimeError(vm, "Undefined property '%s'.", name->chars);
		return false;
	}

	bluObjBoundMethod* bound = bluNewBoundMethod(vm, bluPop(vm), AS_CLOSURE(method));
	bluPush(vm, OBJ_VAL(bound));

	return true;
}

static bool importModule(bluVM* vm, bluObjString* moduleName) {
	for (int32_t i = 0; i < vm->modules.count; i++) {
		if (bluValuesEqual(OBJ_VAL(moduleName), OBJ_VAL(vm->modules.data[i].name))) {
			bluModule* module = &vm->modules.data[i];

			if (module->loaded == true) return true;

			module->loaded = true;

			int32_t frameCountStart = vm->frameCountStart;
			vm->frameCountStart = vm->frameCount;

			module->loader(vm);

			vm->frameCountStart = frameCountStart;

			return true;
		}
	}

	runtimeError(vm, "No such module '%s'", moduleName->chars);

	return false;
}

static bool importFile(bluVM* vm, bluObjString* moduleName) {
	char* path = realpath(moduleName->chars, NULL);
	if (path == NULL) {
		runtimeError(vm, "No such module '%s'.", moduleName->chars);
		return false;
	}

	bluObjString* pathString = bluTakeString(vm, path, strlen(path));

	for (int32_t i = 0; i < vm->modules.count; i++) {
		if (bluValuesEqual(OBJ_VAL(pathString), OBJ_VAL(vm->modules.data[i].name))) {
			if (vm->modules.data[i].loaded == true) return true;
		}
	}

	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		runtimeError(vm, "Could not open file '%s'.", path);
		return false;
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	if (buffer == NULL) {
		runtimeError(vm, "Not enough memory to read \"%s\".\n", path);
		return false;
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	if (bytesRead < fileSize) {
		runtimeError(vm, "Could not read file \"%s\".\n", path);
		return false;
	}

	buffer[bytesRead] = '\0';

	fclose(file);

	bluModule module;
	module.name = pathString;
	module.loader = NULL;
	module.loaded = true;
	module.source = buffer;

	bluModuleBufferWrite(&vm->modules, module);

	int32_t frameCountStart = vm->frameCountStart;
	vm->frameCountStart = vm->frameCount;

	bluInterpret(vm, module.source, module.name->chars);

	vm->frameCountStart = frameCountStart;

	return true;
}

static bool import(bluVM* vm, bluObjString* moduleName) {
	bool isFile = strcmp(".blu", &moduleName->chars[moduleName->length - 4]) == 0;

	return isFile ? importFile(vm, moduleName) : importModule(vm, moduleName);
}

static bluInterpretResult run(bluVM* vm) {

	register bluCallFrame* frame;
	register bluValue* slots;

#define PUSH(value) bluPush(vm, value)
#define POP() bluPop(vm)
#define DROP() (--(vm->stackTop))
#define PEEK(distance) bluPeek(vm, distance)
#define READ_BYTE() (*(frame->ip++))
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.data[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

#define RUNTIME_ERROR(...) runtimeError(vm, __VA_ARGS__)

#define LOAD_FRAME()                                                                                                   \
	frame = &vm->frames[vm->frameCount - 1];                                                                           \
	slots = frame->slots;

	LOAD_FRAME();

	while (true) {

		if (vm->shouldGC) bluCollectGarbage(vm);

#ifdef DEBUG_VM_TRACE
		bluDisassembleInstruction(&frame->closure->function->chunk,
								  frame->ip - frame->closure->function->chunk.code.data);
#endif

		uint8_t instruction = READ_BYTE();

		switch (instruction) {

		case OP_CONSTANT: {
			PUSH(READ_CONSTANT());
			break;
		}

		case OP_FALSE: {
			PUSH(BOOL_VAL(false));
			break;
		}

		case OP_NIL: {
			PUSH(NIL_VAL);
			break;
		}

		case OP_TRUE: {
			PUSH(BOOL_VAL(true));
			break;
		}

		case OP_ARRAY: {
			uint16_t len = READ_SHORT();

			bluObjArray* array = bluNewArray(vm, len);

			for (uint16_t i = 0; i < len; i++) {
				// Expressions are on tzhe stack in reverse order to which we want them in the array.
				array->data[len - i - 1] = POP();
			}

			PUSH(OBJ_VAL(array));

			break;
		}

		case OP_POP: {
			DROP();
			break;
		}

		case OP_GET_LOCAL: {
			uint16_t slot = READ_SHORT();
			PUSH(slots[slot]);
			break;
		}

		case OP_SET_LOCAL: {
			uint16_t slot = READ_SHORT();
			slots[slot] = PEEK(0);
			break;
		}

		case OP_DEFINE_GLOBAL: {
			bluObjString* name = READ_STRING();

			bluTableSet(vm, &vm->globals, name, POP());
			break;
		}

		case OP_GET_GLOBAL: {
			bluObjString* name = READ_STRING();
			bluValue value;

			if (!bluTableGet(vm, &vm->globals, name, &value)) {
				RUNTIME_ERROR("Undefined global variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}

			PUSH(value);
			break;
		}

		case OP_SET_GLOBAL: {
			bluObjString* name = READ_STRING();

			if (bluTableSet(vm, &vm->globals, name, PEEK(0))) {
				bluTableDelete(vm, &vm->globals, name);
				RUNTIME_ERROR("Undefined global variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_GET_UPVALUE: {
			uint16_t slot = READ_SHORT();
			PUSH(*frame->closure->upvalues.data[slot]->value);

			break;
		}

		case OP_SET_UPVALUE: {
			uint16_t slot = READ_SHORT();
			*frame->closure->upvalues.data[slot]->value = PEEK(0);
			break;
		}

		case OP_GET_PROPERTY: {
			bluValue receiver = PEEK(0);
			bluObjString* name = READ_STRING();

			if (!IS_INSTANCE(receiver) && !IS_CLASS(receiver)) {
				RUNTIME_ERROR("Only instances and objects have properties.");
				return INTERPRET_RUNTIME_ERROR;
			} else if (IS_INSTANCE(receiver)) {
				bluValue value;
				if (bluTableGet(vm, &AS_INSTANCE(receiver)->fields, name, &value)) {
					DROP(); // Receiver.
					PUSH(value);
					break;
				}
			} else {
				bluValue value;
				if (bluTableGet(vm, &AS_CLASS(receiver)->fields, name, &value)) {
					DROP(); // Receiver.
					PUSH(value);
					break;
				}
			}

			if (!bindMethod(vm, bluGetClass(vm, receiver), name)) {
				RUNTIME_ERROR("No such property.");
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_SET_PROPERTY: {
			bluValue receiver = PEEK(1);

			if (!IS_INSTANCE(receiver) && !IS_CLASS(receiver)) {
				RUNTIME_ERROR("Only instances and objects have properties.");
				return INTERPRET_RUNTIME_ERROR;
			} else if (IS_INSTANCE(receiver)) {
				bluTableSet(vm, &AS_INSTANCE(receiver)->fields, READ_STRING(), PEEK(0));
			} else {
				bluTableSet(vm, &AS_CLASS(receiver)->fields, READ_STRING(), PEEK(0));
			}

			bluValue value = POP();
			DROP(); // Receiver.
			PUSH(value);

			break;
		}

		case OP_GET_SUPER: {
			bluObjString* name = READ_STRING();
			bluObjClass* superclass = AS_CLASS(POP());

			if (!bindMethod(vm, superclass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_SUBSCRIPT_GET: {
			bluValue index = POP();

			if (!IS_NUMBER(index)) {
				RUNTIME_ERROR("Array index has to be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue receiver = POP();

			if (!IS_ARRAY(receiver) && !IS_STRING(receiver)) {
				RUNTIME_ERROR("Only arrays and strings can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluObjClass* class = bluGetClass(vm, receiver);

			bluValue method;

			if (!bluTableGet(vm, &class->methods, bluCopyString(vm, "len", 3), &method)) {
				RUNTIME_ERROR("No method 'len' on subscript receiver.");
				return INTERPRET_RUNTIME_ERROR;
			}

			PUSH(receiver);
			callValue(vm, method, 0);
			int len = AS_NUMBER(POP());

			if (AS_NUMBER(index) < 0 || AS_NUMBER(index) >= len) {
				RUNTIME_ERROR("Index out of bounds.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (!bluTableGet(vm, &class->methods, bluCopyString(vm, "at", 2), &method)) {
				RUNTIME_ERROR("No method 'at' on subscript receiver.");
				return INTERPRET_RUNTIME_ERROR;
			}

			PUSH(receiver);
			PUSH(index);

			callValue(vm, method, 1);

			break;
		}

		case OP_SUBSCRIPT_SET: {
			bluValue value = POP();
			bluValue index = POP();

			if (!IS_NUMBER(index)) {
				RUNTIME_ERROR("Array index has to be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue array = PEEK(0);

			if (!IS_ARRAY(array)) {
				RUNTIME_ERROR("Only arrays can be indexed.");
				return INTERPRET_RUNTIME_ERROR;
			}

			if (AS_NUMBER(index) >= AS_ARRAY(array)->len || AS_NUMBER(index) < 0) {
				RUNTIME_ERROR("Array index out of range.");
				return INTERPRET_RUNTIME_ERROR;
			}

			AS_ARRAY(array)->data[(int)AS_NUMBER(index)] = value;

			break;
		}

		case OP_CALL: {
			uint8_t argCount = READ_BYTE();

			if (!callValue(vm, PEEK(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			LOAD_FRAME();

			break;
		}

		case OP_INVOKE: {
			uint8_t argCount = READ_BYTE();
			bluObjString* name = READ_STRING();

			if (!invoke(vm, name, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			LOAD_FRAME();

			break;
		}

		case OP_SUPER: {
			uint8_t argCount = READ_BYTE();
			bluObjString* name = READ_STRING();
			bluObjClass* superclass = AS_CLASS(POP());

			if (!invokeFromClass(vm, superclass, name, argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			LOAD_FRAME();

			break;
		}

		case OP_JUMP: {
			uint16_t offset = READ_SHORT();
			frame->ip += offset;
			break;
		}

		case OP_JUMP_IF_FALSE: {
			uint16_t offset = READ_SHORT();
			if (bluIsFalsey(PEEK(0))) frame->ip += offset;
			break;
		}

		case OP_JUMP_IF_TRUE: {
			uint16_t offset = READ_SHORT();
			if (!bluIsFalsey(PEEK(0))) frame->ip += offset;
			break;
		}

		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}

		case OP_EQUAL: {
			bluValue right = POP();
			bluValue left = POP();

			bool equal = bluValuesEqual(left, right);

			PUSH(BOOL_VAL(equal));
			break;
		}

		case OP_NOT_EQUAL: {
			bluValue right = POP();
			bluValue left = POP();

			bool notEqual = !bluValuesEqual(left, right);

			PUSH(BOOL_VAL(notEqual));
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
			if (IS_STRING(PEEK(0)) && IS_STRING(PEEK(1))) {
				concatenate(vm);
			} else if (IS_NUMBER(PEEK(0)) && IS_NUMBER(PEEK(1))) {
				double left = AS_NUMBER(POP());
				double right = AS_NUMBER(POP());

				PUSH(NUMBER_VAL(left + right));
			} else {
				RUNTIME_ERROR("Operands must be both numbers or strings.");
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_DIVIDE: {
			BINARY_OP(NUMBER_VAL, /);
			break;
		}

		case OP_REMINDER: {
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) {
				RUNTIME_ERROR("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluValue right = POP();
			bluValue left = POP();

			double result = (int)AS_NUMBER(left) % (int)AS_NUMBER(right);

			PUSH(NUMBER_VAL(result));
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

		case OP_POWER: {
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) {
				RUNTIME_ERROR("Operands must be numbers.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double exponent = AS_NUMBER(POP());
			double base = AS_NUMBER(POP());
			PUSH(NUMBER_VAL(pow(base, exponent)));
			break;
		}

		case OP_NOT: {
			bluValue value = POP();

			PUSH(BOOL_VAL(bluIsFalsey(value)));
			break;
		}

		case OP_NEGATE: {
			if (!IS_NUMBER(PEEK(0))) {
				RUNTIME_ERROR("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}

			double number = AS_NUMBER(POP());

			PUSH(NUMBER_VAL(-number));

			break;
		}

		case OP_CLOSE_OPVALUE: {
			closeUpvalues(vm, vm->stackTop - 1);
			DROP();
			break;
		}

		case OP_CLOSURE: {
			bluObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			bluObjClosure* closure = newClosure(vm, function);
			PUSH(OBJ_VAL(closure));

			for (uint16_t i = 0; i < function->upvalueCount; i++) {
				bool isLocal = READ_BYTE();
				uint16_t index = READ_SHORT();

				if (isLocal) {
					// Make an new upvalue to close over the parent's local variable.
					bluObjUpvalueBufferWrite(&closure->upvalues, captureUpvalue(vm, slots + index));
				} else {
					// Use the same upvalue as the current call frame.
					bluObjUpvalueBufferWrite(&closure->upvalues, frame->closure->upvalues.data[index]);
				}
			}

			break;
		}

		case OP_CLASS: {
			PUSH(OBJ_VAL(bluNewClass(vm, READ_STRING())));
			break;
		}

		case OP_INHERIT: {
			bluValue superclass = PEEK(1);
			if (!IS_CLASS(superclass)) {
				RUNTIME_ERROR("Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			bluObjClass* subclass = AS_CLASS(POP());
			subclass->superclass = AS_CLASS(superclass);

			break;
		}

		case OP_METHOD: {
			bluValue method = bluPop(vm);
			bluObjClass* class = AS_CLASS(bluPop(vm));
			bluTableSet(vm, &class->methods, READ_STRING(), method);
			break;
		}

		case OP_METHOD_FOREIGN: {
			bluObjString* name = READ_STRING();
			bluObjClass* class = AS_CLASS(POP());
			bluTableSet(vm, &class->methods, name, OBJ_VAL(bluNewNative(vm, NULL, -1)));
			break;
		}

		case OP_METHOD_STATIC: {
			bluValue method = bluPop(vm);
			bluObjClass* class = AS_CLASS(bluPop(vm));
			bluTableSet(vm, &class->fields, READ_STRING(), method);
			break;
		}

		case OP_IMPORT: {
			bluObjString* name = READ_STRING();
			if (!import(vm, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}

		case OP_ECHO: {
			bluPrintValue(POP());
			printf("\n");
			break;
		}

		case OP_RETURN: {
			bluValue result = POP();
			vm->frameCount--;

			closeUpvalues(vm, slots);

			if (vm->frameCount == vm->frameCountStart) {
#if DEBUG
				if (vm->stackTop - vm->stack != 0) {
					RUNTIME_ERROR("Stack not empty!");
					return INTERPRET_RUNTIME_ERROR;
				}
#endif
				return INTERPRET_OK;
			}

			vm->stackTop = slots;

			PUSH(result);

			LOAD_FRAME();
			break;
		}

		case OP_ASSERT: {
			bluValue value = POP();

			if (bluIsFalsey(value)) {
				RUNTIME_ERROR("Assertion failed.");
				return INTERPRET_ASSERTION_ERROR;
			}

			break;
		}

		default: {
			RUNTIME_ERROR("Unknown opcode.");
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

bluVM* bluNewVM() {
	bluVM* vm = malloc(sizeof(bluVM));

	resetStack(vm);

	vm->frameCount = 0;
	vm->frameCountStart = 0;

	vm->openUpvalues = NULL;
	vm->objects = NULL;

	vm->bytesAllocated = 0;
	vm->nextGC = 1024 * 1024;
	vm->shouldGC = false;
	vm->timeGC = 0;

	bluTableInit(vm, &vm->globals);
	bluTableInit(vm, &vm->strings);

	bluModuleBufferInit(&vm->modules);

	vm->stringInitializer = bluCopyString(vm, "__init", 6);

	bluInitStd(vm);

	return vm;
}

void bluFreeVM(bluVM* vm) {
	bluCollectMemory(vm);

	bluTableFree(vm, &vm->globals);
	bluTableFree(vm, &vm->strings);

	for (int32_t i = 0; i < vm->modules.count; i++) {
		if (vm->modules.data[i].source != NULL) {
			free(vm->modules.data[i].source);
		}
	}

	bluModuleBufferFree(&vm->modules);

#ifdef DEBUG
	assert(vm->bytesAllocated == 0);
#endif

	free(vm);
}

bluInterpretResult bluInterpret(bluVM* vm, const char* source, const char* name) {
	bluObjFunction* function = bluCompile(vm, source, name);
	bluObjClosure* closure = newClosure(vm, function);

	if (function == NULL) {
		return INTERPRET_COMPILE_ERROR;
	} else {
		callValue(vm, OBJ_VAL(closure), 0);

		return run(vm);
	}
}

bluObjClass* bluGetClass(bluVM* vm, bluValue value) {
	switch (value.type) {
	case VAL_NIL: return vm->nilClass;
	case VAL_BOOL: return vm->boolClass;
	case VAL_NUMBER: return vm->numberClass;
	case VAL_OBJ:
		if (IS_CLASS(value)) {
			// Core class were created before Class so they don't have a reference to it.
			return vm->classClass;
		} else if (IS_STRING(value)) {
			// String class can be defiend after some string instances have been already allocated so they have
			// incorrect pointer to String class.
			return vm->stringClass;
		} else {
			return AS_OBJ(value)->class;
		}
	}

	__builtin_unreachable();
}

bluObj* bluGetGlobal(bluVM* vm, const char* name) {
	bluValue value;
	if (!bluTableGet(vm, &vm->globals, bluCopyString(vm, name, strlen(name)), &value)) {
		return NULL;
	}

	if (!IS_OBJ(value)) {
		return NULL;
	}

	return AS_OBJ(value);
}

bool bluDefineMethod(bluVM* vm, bluObj* obj, const char* name, bluNativeFn function, int8_t arity) {
	if (obj->type != OBJ_CLASS) {
		return false;
	}

	bluObjNative* native = bluNewNative(vm, function, arity);

	bluObjClass* class = (bluObjClass*)obj;
	return bluTableSet(vm, &class->methods, bluCopyString(vm, name, strlen(name)), OBJ_VAL(native));
}

bool bluDefineStaticMethod(bluVM* vm, bluObj* obj, const char* name, bluNativeFn function, int8_t arity) {
	if (obj->type != OBJ_CLASS) {
		return false;
	}

	bluObjNative* native = bluNewNative(vm, function, arity);

	bluObjClass* class = (bluObjClass*)obj;
	return bluTableSet(vm, &class->fields, bluCopyString(vm, name, strlen(name)), OBJ_VAL(native));
}

void bluRegisterModule(bluVM* vm, const char* name, bluModuleLoader loader) {
	bluModule module;
	module.name = bluCopyString(vm, name, strlen(name));
	module.loader = loader;
	module.loaded = false;
	module.source = NULL;

	bluModuleBufferWrite(&vm->modules, module);
}
