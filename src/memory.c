#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_TRACE_GC
#include "debug.h"
#include <stdio.h>
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* previous, size_t oldSize, size_t newSize) {
	vm.bytesAllocated += newSize - oldSize;

	if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
		collectGarbage();
#endif

		if (vm.bytesAllocated > vm.nextGC) {
			collectGarbage();
		}
	}

	if (newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

void grayObject(Obj* object) {
	if (object == NULL) return;

	// Don't get caught in cycle.
	if (object->isDark) return;

#ifdef DEBUG_TRACE_GC
	printf("%p gray ", object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

	object->isDark = true;

	if (vm.grayCapacity < vm.grayCount + 1) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

		// Not using reallocate() here because we don't want to trigger the GC inside a GC!
		vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
	}

	vm.grayStack[vm.grayCount++] = object;
}

void grayValue(Value value) {
	if (!IS_OBJ(value)) return;
	grayObject(AS_OBJ(value));
}

static void grayArray(ValueArray* array) {
	for (int i = 0; i < array->count; i++) {
		grayValue(array->values[i]);
	}
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_TRACE_GC
	printf("%p blacken ", object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

	switch (object->type) {
	case OBJ_ARRAY: {
		ObjArray* array = (ObjArray*)object;
		for (uint32_t i = 0; i < array->cap; i++) {
			grayValue(array->data[i]);
		}
		break;
	}

	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		grayObject((Obj*)closure->function);
		for (int i = 0; i < closure->upvalueCount; i++) {
			grayObject((Obj*)closure->upvalues[i]);
		}
		break;
	}

	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		grayObject((Obj*)function->name);
		grayArray(&function->chunk.constants);
		break;
	}

	case OBJ_UPVALUE: grayValue(((ObjUpvalue*)object)->closed); break;

	case OBJ_NATIVE:
	case OBJ_STRING:
		// No references.
		break;
	}
}

static void freeObject(Obj* object) {
#ifdef DEBUG_TRACE_GC
	printf("%p free ", object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

	switch (object->type) {

	case OBJ_ARRAY: {
		ObjArray* array = (ObjArray*)object;
		FREE_ARRAY(Value, array->data, array->cap);
		FREE(ObjArray, object);
		break;
	}

	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		FREE_ARRAY(Value, closure->upvalues, closure->upvalueCount);
		FREE(ObjClosure, object);
		break;
	}

	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		freeChunk(&function->chunk);
		FREE(ObjFunction, object);
		break;
	}

	case OBJ_NATIVE: {
		FREE(ObjNative, object);
		break;
	}

	case OBJ_STRING: {
		ObjString* string = (ObjString*)object;
		FREE_ARRAY(char, string->chars, string->length + 1);
		FREE(ObjString, object);
		break;
	}

	case OBJ_UPVALUE: {
		FREE(ObjUpvalue, object);
		break;
	}
	}
}

void collectGarbage() {
#ifdef DEBUG_TRACE_GC
	printf("-- gc begin\n");
	size_t before = vm.bytesAllocated;
#endif

	// Mark the stack roots.
	for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		grayValue(*slot);
	}

	for (int i = 0; i < vm.frameCount; i++) {
		grayObject((Obj*)vm.frames[i].closure);
	}

	// Mark the open upvalues.
	for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
		grayObject((Obj*)upvalue);
	}

	// Mark the global roots.
	grayTable(&vm.globals);
	grayCompilerRoots();

	// Traverse the references.
	while (vm.grayCount > 0) {
		// Pop an item from the gray stack.
		Obj* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}

	// Delete unused interned strings.
	tableRemoveWhite(&vm.strings);

	// Collect the white objects.
	Obj** object = &vm.objects;
	while (*object != NULL) {
		if (!((*object)->isDark)) {
			// This object wasn't reached, so remove it from the list and free it.
			Obj* unreached = *object;
			*object = unreached->next;
			freeObject(unreached);
		} else {
			// This object was reached, so unmark it (for the next GC) and move on to the next.
			(*object)->isDark = false;
			object = &(*object)->next;
		}
	}

	// Adjust the heap size based on live memory.
	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_TRACE_GC
	printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm.bytesAllocated, before,
		   vm.bytesAllocated, vm.nextGC);
#endif
}

/**
 * TODO: Store objects in a hierarchical way so we can safely free them.
 */
void freeObjects() {
	// Obj* object = vm.objects;
	// while (object != NULL) {
	// 	Obj* next = object->next;
	// 	freeObject(object);
	// 	object = next;
	// }

	free(vm.grayStack);
}
