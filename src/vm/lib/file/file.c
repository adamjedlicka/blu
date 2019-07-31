#define _GNU_SOURCE

#include <stdio.h>

#include "file.h"
#include "vm/memory.h"
#include "vm/object.h"
#include "vm/value.h"

#include "file.blu.inc"

typedef struct {
	FILE* fd;
} FileData;

void construct(bluVM* vm, bluObjInstance* instance) {
	instance->data = bluAllocate(vm, sizeof(FileData));
	((FileData*)instance->data)->fd = NULL;
}

void destruct(bluVM* vm, bluObjInstance* instance) {
	fclose(((FileData*)instance->data)->fd);

	bluDeallocate(vm, instance->data, sizeof(FileData));
}

int8_t File_open(bluVM* vm, int8_t argCount, bluValue* args) {
	bluValue value;
	bluObjInstance* receiver = AS_INSTANCE(args[0]);

	bluTableGet(vm, &receiver->fields, bluCopyString(vm, "_name", strlen("_name")), &value);
	bluObjString* name = AS_STRING(value);

	bluTableGet(vm, &receiver->fields, bluCopyString(vm, "_mode", strlen("_mode")), &value);
	bluObjString* mode = AS_STRING(value);

	FILE* file = fopen(name->chars, mode->chars);
	if (file == NULL) return -1;

	((FileData*)receiver->data)->fd = file;

	return 1;
}

int8_t File_close(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjInstance* receiver = AS_INSTANCE(args[0]);

	fclose(((FileData*)receiver->data)->fd);

	return 1;
}

int8_t File_rewind(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjInstance* receiver = AS_INSTANCE(args[0]);

	rewind(((FileData*)receiver->data)->fd);

	return 1;
}

int8_t File_readLine(bluVM* vm, int8_t argCount, bluValue* args) {
	bluObjInstance* receiver = AS_INSTANCE(args[0]);

	FILE* file = ((FileData*)receiver->data)->fd;

	char* buffer = NULL;
	size_t len = 0;

	ssize_t read = getline(&buffer, &len, file);
	if (read == -1) {
		args[0] = NIL_VAL;
		free(buffer);
		return 1;
	}

	buffer[read - 1] = '\0';

	args[0] = OBJ_VAL(bluCopyString(vm, buffer, read - 1));

	free(buffer);

	return 1;
}

void bluInitFile(bluVM* vm) {
	bluInterpret(vm, fileSource, "__FILE__");

	bluObj* fileClass = bluGetGlobal(vm, "File");

	AS_CLASS(OBJ_VAL(fileClass))->construct = construct;
	AS_CLASS(OBJ_VAL(fileClass))->destruct = destruct;

	bluDefineMethod(vm, fileClass, "open", File_open, 0);
	bluDefineMethod(vm, fileClass, "close", File_close, 0);
	bluDefineMethod(vm, fileClass, "rewind", File_rewind, 0);
	bluDefineMethod(vm, fileClass, "readLine", File_readLine, 0);
}
