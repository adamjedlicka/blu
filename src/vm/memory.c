#include <time.h>

#include "memory.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 0.5
#define GC_HEAP_MINIMUM 1024 * 1024

#define GC_DEBUG_STRESS true
#define GC_DEBUG_TRACE true

static void freeObject(bluVM* vm, bluObj* object) {
	if (GC_DEBUG_TRACE) {
		printf("%p free ", object);
		bluPrintValue(OBJ_VAL(object));
		printf("\n");
	}

	switch (object->type) {

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluChunkFree(&function->chunk);
		bluDeallocate(vm, function, sizeof(bluObjFunction));
		break;
	}

	case OBJ_STRING: {
		bluObjString* string = (bluObjString*)object;
		bluDeallocate(vm, string->chars, sizeof(char) * (string->length + 1));
		bluDeallocate(vm, string, sizeof(bluObjString));
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

	if (GC_DEBUG_TRACE) {
		printf("%p gray ", object);
		bluPrintValue(OBJ_VAL(object));
		printf("\n");
	}

	object->isDark = true;

	switch (object->type) {

	case OBJ_FUNCTION: {
		bluObjFunction* function = (bluObjFunction*)object;
		bluGrayObject(vm, (bluObj*)function->name);
		bluGrayValueBuffer(vm, &function->chunk.constants);
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

	if (GC_DEBUG_STRESS || vm->bytesAllocated > vm->nextGC) {
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
	if (GC_DEBUG_TRACE) printf("-- gc begin\n");

	size_t before = vm->bytesAllocated;
	double start = (double)clock() / CLOCKS_PER_SEC;

	for (bluValue* slot = vm->stack; slot < vm->stackTop; slot++) {
		bluGrayValue(vm, *slot);
	}

	for (int32_t i = 0; i < vm->frameCount; i++) {
		bluGrayObject(vm, (bluObj*)vm->frames[i].function);
	}

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

	if (GC_DEBUG_TRACE)
		printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->bytesAllocated, before,
			   vm->bytesAllocated, vm->nextGC);
}

void bluCollectMemory(bluVM* vm) {
	bluObj* object = vm->objects;
	while (object != NULL) {
		bluObj* next = object->next;
		freeObject(vm, object);
		object = next;
	}
}
