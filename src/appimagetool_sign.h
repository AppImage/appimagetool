#ifndef APPIMAGETOOL_SIGN_H
#define APPIMAGETOOL_SIGN_H

#ifdef __cplusplus
extern "C" {
#endif


bool sign_appimage(char* appimage_filename, char* key_id, bool verbose);

/**
 * Release resources held due to the initialization of GPG related libraries.
 * Should be called every time before the application is terminated if any such functionality was used.
 */
void gpg_release_resources();

// it's possible to use a single macro to error-check both gpgme and gcrypt, since both originate from the gpg project
// the error types gcry_error_t and gpgme_error_t are both aliases for gpg_error_t
#define gpg_check_call(call_to_function) \
    { \
        gpg_error_t error = (call_to_function); \
        if (error != GPG_ERR_NO_ERROR) { \
            fprintf(stderr, "[sign] %s: call failed: %s\n", #call_to_function, gpgme_strerror(error)); \
            gpg_release_resources(); \
            return false; \
        } \
    }

/**
 * Init libgcrypt. Must be done once in every application if one wants to use this library.
 * @return true on success, false otherwise
 */
bool init_gcrypt();

#ifdef __cplusplus
}
#endif

#endif /* APPIMAGETOOL_SIGN_H */