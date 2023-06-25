#pragma once


#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t lo;
    uint32_t hi;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint8_t  buffer[64];
    uint32_t block[16];
} Md5Context;

#define MD5_HASH_SIZE (128 / 8)

typedef struct {
    uint8_t bytes[MD5_HASH_SIZE];
} MD5_HASH;

// initialize new context
void Md5Initialise(Md5Context* ctx);

// add data to the context
void Md5Update(Md5Context* ctx, void const* buf, uint32_t bufSize);

// calculate final digest from context
void Md5Finalise(Md5Context* ctx, MD5_HASH* digest);

// create new context, add data from buffer to it, and calculate digest
void Md5Calculate(void const* Buffer, uint32_t BufferSize, MD5_HASH* Digest);
