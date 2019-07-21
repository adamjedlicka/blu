#ifndef blu_buffer_h
#define blu_buffer_h

#include "blu.h"
#include "utils.h"

// We need buffers of a few different types. To avoid lots of casting between void* and back, we'll use the preprocessor
// as a poor man's generics and let it generate a few type-specific ones.
#define DECLARE_BUFFER(name, type)                                                                                     \
	typedef struct {                                                                                                   \
		type* data;                                                                                                    \
		int32_t count;                                                                                                 \
		int32_t capacity;                                                                                              \
	} name##Buffer;                                                                                                    \
	void name##BufferInit(name##Buffer* buffer);                                                                       \
	void name##BufferFree(name##Buffer* buffer);                                                                       \
	void name##BufferFill(name##Buffer* buffer, type data, int32_t count);                                             \
	int32_t name##BufferWrite(name##Buffer* buffer, type data)

// This should be used once for each type instantiation, somewhere in a .c file.
#define DEFINE_BUFFER(name, type)                                                                                      \
	void name##BufferInit(name##Buffer* buffer) {                                                                      \
		buffer->data = NULL;                                                                                           \
		buffer->capacity = 0;                                                                                          \
		buffer->count = 0;                                                                                             \
	}                                                                                                                  \
                                                                                                                       \
	void name##BufferFree(name##Buffer* buffer) {                                                                      \
		if (buffer->data != NULL) free(buffer->data);                                                                  \
		name##BufferInit(buffer);                                                                                      \
	}                                                                                                                  \
                                                                                                                       \
	void name##BufferFill(name##Buffer* buffer, type data, int32_t count) {                                            \
		if (buffer->capacity < buffer->count + count) {                                                                \
			int32_t capacity = bluPowerOf2Ceil(buffer->count + count);                                                 \
			buffer->data = (type*)realloc(buffer->data, capacity * sizeof(type));                                      \
			buffer->capacity = capacity;                                                                               \
		}                                                                                                              \
                                                                                                                       \
		for (int32_t i = 0; i < count; i++) {                                                                          \
			buffer->data[buffer->count++] = data;                                                                      \
		}                                                                                                              \
	}                                                                                                                  \
                                                                                                                       \
	int32_t name##BufferWrite(name##Buffer* buffer, type data) {                                                       \
		name##BufferFill(buffer, data, 1);                                                                             \
		return buffer->count - 1;                                                                                      \
	}

DECLARE_BUFFER(Byte, uint8_t);
DECLARE_BUFFER(Int, int32_t);

#endif
