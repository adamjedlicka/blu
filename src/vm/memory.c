#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG
#include <time.h>
#endif

#ifdef DEBUG_TRACE_GC
#include "debug.h"
#include <stdio.h>
#endif

#define GC_HEAP_GROW_FACTOR 0.5
#define GC_HEAP_MINIMUM 1024 * 1024

void* bluReallocate(bluVM* vm, void* previous, size_t oldSize, size_t newSize) {
	vm->bytesAllocated += newSize - oldSize;

	if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
		bluCollectGarbage(vm);
#endif

		if (vm->bytesAllocated > vm->nextGC) {
			bluCollectGarbage(vm);
		}
	}

	if (newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

void bluGrayObject(bluVM* vm, bluObj* object) {
	if (object == NULL) return;

	// Don't get caught in cycle.
	if (object->isDark) return;

	object->isDark = true;

	if (vm->grayCapacity < vm->grayCount + 1) {
		vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);

		// Not using reallocate() here because we don't want to trigger the GC inside a GC!
		vm->grayStack = realloc(vm->grayStack, sizeof(bluObj*) * vm->grayCapacity);
	}

	vm->grayStack[vm->grayCount++] = object;
}

void bluGrayValue(bluVM* vm, bluValue value) {
	if (!IS_OBJ(value)) return;
	bluGrayObject(vm, AS_OBJ(value));
}

static void grayArray(bluVM* vm, bluValueArray* array) {
	for (int i = 0; i < array->count; i++) {
		bluGrayValue(vm, array->values[i]);
	}
}

static void blackenObject(bluVM* vm, bluObj* object) {

	switch (object->type) {

	case OBJ_ARRAY: {
		bluObjArray* array = (bluObjArray*)object;
		for (uint32_t i = 0; i < array->cap; i++) {
			bluGrayValue(vm, array->data[i]);
		}
		break;
	}

	case OBJ_BOUND_METHOD: {
		bluObjBoundMethod* bound = (bluObjBoundMethod*)object;
		bluGrayValue(vm, bound->receiver);
		bluGrayObject(vm, (bluObj*)bound->method);
		break;
	}

	case OBJ_CLASS: {
		bluObjClass* klass = (bluObjClass*)object;
		bluGrayObject(vm, (bluObj*)klass->name);
		bluGrayTable(vm, &klass->methods);
		break;
	}

	case OBJ_CLOSURE: {
		bluObjClosure* closure = (bluObjClosure*)object;
		bluGrayObject(vm, (bluObj*)closure->function);
		for (int i = 0; i < closure->upvalueCount; i++) {
			bluGrayObject(vm, (bluObj*)closure->upvalues[i]);
		}
		break;
	}

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluGrayObject(vm, (bluObj*)function->name);
		grayArray(vm, &function->chunk.constants);
		break;
	}

	case OBJ_INSTANCE: {
		bluObjInstance* instance = (bluObjInstance*)object;
		bluGrayObject(vm, (bluObj*)instance->obj.klass);
		bluGrayTable(vm, &instance->fields);
		break;
	}

	case OBJ_NATIVE: {
		break;
	}

	case OBJ_STRING: {
		break;
	}

	case OBJ_UPVALUE: {
		bluGrayValue(vm, ((bluObjUpvalue*)object)->closed);
		break;
	}
	}
}

static void freeObject(bluVM* vm, bluObj* object) {

	switch (object->type) {

	case OBJ_ARRAY: {
		bluObjArray* array = (bluObjArray*)object;
		FREE_ARRAY(vm, bluValue, array->data, array->cap);
		FREE(vm, bluObjArray, object);
		break;
	}

	case OBJ_BOUND_METHOD: {
		FREE(vm, bluObjBoundMethod, object);
		break;
	}

	case OBJ_CLASS: {
		bluObjClass* klass = (bluObjClass*)object;
		bluFreeTable(vm, &klass->methods);
		FREE(vm, bluObjClass, object);
		break;
	}

	case OBJ_CLOSURE: {
		bluObjClosure* closure = (bluObjClosure*)object;
		FREE_ARRAY(vm, bluValue, closure->upvalues, closure->upvalueCount);
		FREE(vm, bluObjClosure, object);
		break;
	}

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluFreeChunk(vm, &function->chunk);
		FREE(vm, bluObjFunction, object);
		break;
	}

	case OBJ_INSTANCE: {
		bluObjInstance* instance = (bluObjInstance*)object;
		bluFreeTable(vm, &instance->fields);
		FREE(vm, bluObjInstance, object);
		break;
	}

	case OBJ_NATIVE: {
		FREE(vm, bluObjNative, object);
		break;
	}

	case OBJ_STRING: {
		bluObjString* string = (bluObjString*)object;
		FREE_ARRAY(vm, char, string->chars, string->length + 1);
		FREE(vm, bluObjString, object);
		break;
	}

	case OBJ_UPVALUE: {
		FREE(vm, bluObjUpvalue, object);
		break;
	}
	}
}

void bluCollectGarbage(bluVM* vm) {
#ifdef DEBUG_TRACE_GC
	printf("-- gc begin\n");
	size_t before = vm->bytesAllocated;
#endif

#ifdef DEBUG
	double start = (double)clock() / CLOCKS_PER_SEC;
#endif

	// Mark the stack roots.
	for (bluValue* slot = vm->stack; slot < vm->stackTop; slot++) {
		bluGrayValue(vm, *slot);
	}

	for (int i = 0; i < vm->frameCount; i++) {
		bluGrayObject(vm, (bluObj*)vm->frames[i].closure);
	}

	// Mark the open upvalues.
	for (bluObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
		bluGrayObject(vm, (bluObj*)upvalue);
	}

	// Mark the global roots.
	bluGrayTable(vm, &vm->globals);
	bluGrayCompilerRoots(vm);
	bluGrayObject(vm, (bluObj*)vm->initString);

	// Traverse the references.
	while (vm->grayCount > 0) {
		// Pop an item from the gray stack.
		bluObj* object = vm->grayStack[--vm->grayCount];
		blackenObject(vm, object);
	}

	// Delete unused interned strings.
	bluTableRemoveWhite(vm, &vm->strings);

	// Collect the white objects.
	bluObj** object = &vm->objects;
	while (*object != NULL) {
		if (!((*object)->isDark)) {
			// This object wasn't reached, so remove it from the list and free it.
			bluObj* unreached = *object;
			*object = unreached->next;
			freeObject(vm, unreached);
		} else {
			// This object was reached, so unmark it (for the next GC) and move on to the next.
			(*object)->isDark = false;
			object = &(*object)->next;
		}
	}

	// Adjust the heap size based on live memory.
	vm->nextGC = vm->bytesAllocated < GC_HEAP_MINIMUM ? GC_HEAP_MINIMUM : vm->bytesAllocated * GC_HEAP_GROW_FACTOR;
	// vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG
	double end = (double)clock() / CLOCKS_PER_SEC;
	vm->timeGC += (end - start);
#endif

#ifdef DEBUG_TRACE_GC
	printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->bytesAllocated, before,
		   vm->bytesAllocated, vm->nextGC);
#endif
}

/**
 * TODO: Store objects in a hierarchical way so we can safely free them.
 */
void bluFreeObjects(bluVM* vm) {
	bluObj* object = vm->objects;
	while (object != NULL) {
		bluObj* next = object->next;
		freeObject(vm, object);
		object = next;
	}

	free(vm->grayStack);
}
