#pragma once

/**
 * Download runtime from GitHub into a buffer.
 * This function allocates a buffer of the right size internally, which needs to be cleaned up by the caller.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RUNTIME_TYPE_CLASSIC,
    RUNTIME_TYPE_STATIC,
} RuntimeType;

bool fetch_runtime(char *arch, size_t *size, char **buffer, RuntimeType runtimeType, bool verbose);
#ifdef __cplusplus
}
#endif

