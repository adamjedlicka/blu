#include "memory.h"
#include "vm/common.h"
#include "vm/vm.h"

#define GC_HEAP_GROW_FACTOR 2
#define GC_HEAP_MINIMUM 1024 * 1024

static void freeObject(bluVM* vm, bluObj* object) {

#ifdef DEBUG_GC_TRACE
	printf("%p free ", object);
	bluPrintValue(OBJ_VAL(object));
	printf("\n");
#endif

	switch (object->type) {

	case OBJ_ARRAY: {
		bluObjArray* array = (bluObjArray*)object;
		bluDeallocate(vm, array->data, (sizeof(bluValue) * array->cap));
		bluDeallocate(vm, array, sizeof(bluObjArray));
		break;
	}

	case OBJ_BOUND_METHOD: {
		bluObjBoundMethod* boundMethod = (bluObjBoundMethod*)object;
		bluDeallocate(vm, boundMethod, sizeof(bluObjBoundMethod));
		break;
	}

	case OBJ_CLASS: {
		bluObjClass* class = (bluObjClass*)object;
		bluTableFree(vm, &class->methods);
		bluTableFree(vm, &class->fields);
		bluDeallocate(vm, class, sizeof(bluObjClass));
		break;
	}

	case OBJ_CLOSURE: {
		bluObjClosure* closure = (bluObjClosure*)object;
		bluObjUpvalueBufferFree(&closure->upvalues);
		bluDeallocate(vm, closure, sizeof(bluObjClosure));
		break;
	}

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluChunkFree(&function->chunk);
		bluDeallocate(vm, function, sizeof(bluObjFunction));
		break;
	}

	case OBJ_INSTANCE: {
		bluObjInstance* instance = (bluObjInstance*)object;

		if (instance->obj.class->destruct != NULL) {
			instance->obj.class->destruct(vm, instance);
		}

		bluTableFree(vm, &instance->fields);
		bluDeallocate(vm, instance, sizeof(bluObjInstance));
		break;
	}

	case OBJ_NATIVE: {
		bluObjNative* native = (bluObjNative*)object;
		bluDeallocate(vm, native, sizeof(bluObjNative));
		break;
	}

	case OBJ_UPVALUE: {
		bluObjUpvalue* upvalue = (bluObjUpvalue*)object;
		bluDeallocate(vm, upvalue, sizeof(bluObjUpvalue));
		break;
	}

	case OBJ_STRING: {
		bluObjString* string = (bluObjString*)object;
		bluDeallocate(vm, string, sizeof(bluObjString) + (sizeof(char) * (string->length + 1)));
		break;
	}
	}
}

static void tableDeleteWhite(bluVM* vm, bluTable* table) {
	for (int32_t i = 0; i <= table->capacityMask; i++) {
		bluEntry* entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->obj.isDark) {
			bluTableDelete(vm, table, entry->key);
		}
	}
}

void bluGrayValue(bluVM* vm, bluValue value) {
	if (!IS_OBJ(value)) return;

	bluGrayObject(vm, AS_OBJ(value));
}

void bluGrayObject(bluVM* vm, bluObj* object) {
	if (object == NULL) return;

	if (object->isDark) return;

#ifdef DEBUG_GC_TRACE
	printf("%p gray ", object);
	bluPrintValue(OBJ_VAL(object));
	printf("\n");
#endif

	object->isDark = true;

	bluGrayObject(vm, (bluObj*)object->class);

	switch (object->type) {

	case OBJ_ARRAY: {
		bluObjArray* array = (bluObjArray*)object;
		for (int32_t i = 0; i < array->len; i++) {
			bluGrayValue(vm, array->data[i]);
		}
		break;
	}

	case OBJ_BOUND_METHOD: {
		bluObjBoundMethod* boundMethod = (bluObjBoundMethod*)object;
		bluGrayValue(vm, boundMethod->receiver);
		bluGrayObject(vm, (bluObj*)boundMethod->closure);
		break;
	}

	case OBJ_CLASS: {
		bluObjClass* class = (bluObjClass*)object;
		bluGrayObject(vm, (bluObj*)class->name);
		bluGrayTable(vm, &class->methods);
		bluGrayTable(vm, &class->fields);
		break;
	}

	case OBJ_CLOSURE: {
		bluObjClosure* closure = (bluObjClosure*)object;
		bluGrayObject(vm, (bluObj*)closure->function);
		for (int32_t i = 0; i < closure->upvalues.count; i++) {
			bluGrayObject(vm, &closure->upvalues.data[i]->obj);
		}
		break;
	}

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluGrayObject(vm, (bluObj*)function->name);
		bluGrayValueBuffer(vm, &function->chunk.constants);
		break;
	}

	case OBJ_INSTANCE: {
		bluObjInstance* instance = (bluObjInstance*)object;
		bluGrayTable(vm, &instance->fields);
		break;
	}

	case OBJ_NATIVE: {
		break;
	}

	case OBJ_UPVALUE: {
		bluObjUpvalue* upvalue = (bluObjUpvalue*)object;
		bluGrayValue(vm, upvalue->closed);
		break;
	}

	case OBJ_STRING: {
		break;
	}
	}
}

void bluGrayValueBuffer(bluVM* vm, bluValueBuffer* buffer) {
	for (int32_t i = 0; i < buffer->count; i++) {
		bluGrayValue(vm, buffer->data[i]);
	}
}

void bluGrayTable(bluVM* vm, bluTable* table) {
	for (int32_t i = 0; i < table->capacityMask; i++) {
		bluEntry* entry = &table->entries[i];
		bluGrayObject(vm, (bluObj*)entry->key);
		bluGrayValue(vm, entry->value);
	}
}

void* bluAllocate(bluVM* vm, size_t size) {
	return bluReallocate(vm, NULL, 0, size);
}

void* bluReallocate(bluVM* vm, void* previous, size_t oldSize, size_t newSize) {
	vm->bytesAllocated += newSize - oldSize;

#ifdef DEBUG_GC_STRESS
	vm->shouldGC = true;
#endif

	if (vm->bytesAllocated > vm->nextGC) {
		vm->shouldGC = true;
	}

	if (newSize == 0) {
		free(previous);
		return NULL;
	}

	return realloc(previous, newSize);
}

void bluDeallocate(bluVM* vm, void* pointer, size_t size) {
	bluReallocate(vm, pointer, size, 0);
}

void bluCollectGarbage(bluVM* vm) {
#ifdef DEBUG_GC_TRACE
	printf("-- gc begin\n");

	size_t before = vm->bytesAllocated;
#endif

	double start = (double)clock() / CLOCKS_PER_SEC;

	for (bluValue* slot = vm->stack; slot < vm->stackTop; slot++) {
		bluGrayValue(vm, *slot);
	}

	for (int32_t i = 0; i < vm->frameCount; i++) {
		bluGrayObject(vm, (bluObj*)vm->frames[i].closure);
	}

	for (int32_t i = 0; i < vm->modules.count; i++) {
		bluGrayObject(vm, (bluObj*)vm->modules.data[i].name);
	}

	bluGrayTable(vm, &vm->globals);

	bluGrayObject(vm, (bluObj*)vm->stringInitializer);

	tableDeleteWhite(vm, &vm->strings);

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

	vm->nextGC = vm->bytesAllocated < GC_HEAP_MINIMUM ? GC_HEAP_MINIMUM : vm->bytesAllocated * GC_HEAP_GROW_FACTOR;
	vm->shouldGC = false;
	vm->timeGC += ((double)clock() / CLOCKS_PER_SEC) - start;

#ifdef DEBUG_GC_TRACE
	printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->bytesAllocated, before,
		   vm->bytesAllocated, vm->nextGC);
#endif
}

void bluCollectMemory(bluVM* vm) {
	bluObj* object = vm->objects;
	while (object != NULL) {
		bluObj* next = object->next;
		freeObject(vm, object);
		object = next;
	}
}
