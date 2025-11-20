#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "md5.h"
#include "util.h"

bool appimage_type2_digest_md5(const char* path, char* digest) {
    // skip digest, signature and key sections in digest calculation
    unsigned long digest_md5_offset = 0, digest_md5_length = 0;
    if (!appimage_get_elf_section_offset_and_length(path, ".digest_md5", &digest_md5_offset, &digest_md5_length))
        return false;

    unsigned long signature_offset = 0, signature_length = 0;
    if (!appimage_get_elf_section_offset_and_length(path, ".sha256_sig", &signature_offset, &signature_length))
        return false;

    unsigned long sig_key_offset = 0, sig_key_length = 0;
    if (!appimage_get_elf_section_offset_and_length(path, ".sig_key", &sig_key_offset, &sig_key_length))
        return false;

    Md5Context md5_context;
    Md5Initialise(&md5_context);

    // read file in chunks
    static const int chunk_size = 4096;

    FILE *fp = fopen(path, "r");

    // determine file size
    fseek(fp, 0L, SEEK_END);
    const long file_size = ftell(fp);
    rewind(fp);

    long bytes_left = file_size;

    // if a section spans over more than a single chunk, we need emulate null bytes in the following chunks
    ssize_t bytes_skip_following_chunks = 0;

    while (bytes_left > 0) {
        char buffer[chunk_size];

        long current_position = ftell(fp);

        ssize_t bytes_left_this_chunk = chunk_size;

        // first, check whether there's bytes left that need to be skipped
        if (bytes_skip_following_chunks > 0) {
            ssize_t bytes_skip_this_chunk = (bytes_skip_following_chunks % chunk_size == 0) ? chunk_size : (bytes_skip_following_chunks % chunk_size);
            bytes_left_this_chunk -= bytes_skip_this_chunk;

            // we could just set it to 0 here, but it makes more sense to use -= for debugging
            bytes_skip_following_chunks -= bytes_skip_this_chunk;

            // make sure to skip these bytes in the file
            fseek(fp, bytes_skip_this_chunk, SEEK_CUR);
        }

        // check whether there's a section in this chunk that we need to skip
        if (digest_md5_offset != 0 && digest_md5_length != 0 && (long)digest_md5_offset - current_position > 0 && (long)digest_md5_offset - current_position < chunk_size) {
            ssize_t begin_of_section = ((long)digest_md5_offset - current_position) % chunk_size;
            // read chunk before section
            fread(buffer, sizeof(char), (size_t) begin_of_section, fp);

            bytes_left_this_chunk -= begin_of_section;
            bytes_left_this_chunk -= (ssize_t)digest_md5_length;

            // if bytes_left is now < 0, the section exceeds the current chunk
            // this amount of bytes needs to be skipped in the future sections
            if (bytes_left_this_chunk < 0) {
                bytes_skip_following_chunks = (ssize_t) (-1 * bytes_left_this_chunk);
                bytes_left_this_chunk = 0;
            }

            // if there's bytes left to read, we need to seek the difference between chunk's end and bytes_left
            fseek(fp, (chunk_size - bytes_left_this_chunk - begin_of_section), SEEK_CUR);
        }

        // check whether there's a section in this chunk that we need to skip
        if (signature_offset != 0 && signature_length != 0 && (long)signature_offset - current_position > 0 && (long)signature_offset - current_position < chunk_size) {
            ssize_t begin_of_section = ((long)signature_offset - current_position) % chunk_size;
            // read chunk before section
            fread(buffer, sizeof(char), (size_t) begin_of_section, fp);

            bytes_left_this_chunk -= begin_of_section;
            bytes_left_this_chunk -= (ssize_t)signature_length;

            // if bytes_left is now < 0, the section exceeds the current chunk
            // this amount of bytes needs to be skipped in the future sections
            if (bytes_left_this_chunk < 0) {
                bytes_skip_following_chunks = (ssize_t) (-1 * bytes_left_this_chunk);
                bytes_left_this_chunk = 0;
            }

            // if there's bytes left to read, we need to seek the difference between chunk's end and bytes_left
            fseek(fp, (chunk_size - bytes_left_this_chunk - begin_of_section), SEEK_CUR);
        }

        // check whether there's a section in this chunk that we need to skip
        if (sig_key_offset != 0 && sig_key_length != 0 && (long)sig_key_offset - current_position > 0 && (long)sig_key_offset - current_position < chunk_size) {
            ssize_t begin_of_section = ((long)sig_key_offset - current_position) % chunk_size;
            // read chunk before section
            fread(buffer, sizeof(char), (size_t) begin_of_section, fp);

            bytes_left_this_chunk -= begin_of_section;
            bytes_left_this_chunk -= (ssize_t)sig_key_length;

            // if bytes_left is now < 0, the section exceeds the current chunk
            // this amount of bytes needs to be skipped in the future sections
            if (bytes_left_this_chunk < 0) {
                bytes_skip_following_chunks = (ssize_t) (-1 * bytes_left_this_chunk);
                bytes_left_this_chunk = 0;
            }

            // if there's bytes left to read, we need to seek the difference between chunk's end and bytes_left
            fseek(fp, (chunk_size - bytes_left_this_chunk - begin_of_section), SEEK_CUR);
        }

        // check whether we're done already
        if (bytes_left_this_chunk > 0) {
            // read data from file into buffer with the correct offset in case bytes have to be skipped
            fread(buffer + (chunk_size - bytes_left_this_chunk), sizeof(char), (size_t) bytes_left_this_chunk, fp);
        }

        // feed buffer into checksum calculation
        Md5Update(&md5_context, buffer, chunk_size);

        bytes_left -= chunk_size;
    }

    MD5_HASH checksum;
    Md5Finalise(&md5_context, &checksum);

    memcpy(digest, (const char*) checksum.bytes, 16);

    fclose(fp);

    return true;
}
