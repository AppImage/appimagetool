#pragma once

/**
 * Download runtime from GitHub into a buffer.
 * This function allocates a buffer of the right size internally, which needs to be cleaned up by the caller.
 */
bool fetch_runtime(char* arch, size_t* size, char** buffer, bool verbose);
