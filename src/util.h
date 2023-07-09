#ifdef __cplusplus
extern "C" {
#endif

char* appimage_hexlify(const char* bytes, const size_t numBytes);
bool appimage_get_elf_section_offset_and_length(const char* fname, const char* section_name, unsigned long* offset, unsigned long* length);
bool appimage_type2_digest_md5(const char* path, char* digest);

#ifdef __cplusplus
}
#endif