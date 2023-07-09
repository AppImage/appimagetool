#ifndef ELF_H
#define ELF_H

#include <stdbool.h>

bool appimage_get_elf_section_offset_and_length(const char* fname, const char* section_name, unsigned long* offset, unsigned long* length);

char* read_file_offset_length(const char* fname, unsigned long offset, unsigned long length);

int appimage_print_hex(const char* fname, unsigned long offset, unsigned long length);

int appimage_print_binary(const char* fname, unsigned long offset, unsigned long length);

#endif /* ELF_H */