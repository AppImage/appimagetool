// need to define this to enable asprintf
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>

#include <curl/curl.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#include "appimagetool_fetch_runtime.h"

bool fetch_runtime(char* arch, size_t* size, char** buffer, bool verbose) {
    // not the cleanest approach to globally init curl here, but this method shouldn't be called more than once anyway
    curl_global_init(CURL_GLOBAL_ALL);

    // should be plenty big for the URL
    char url[1024];
    int url_size = snprintf(url, sizeof(url), "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-%s", arch);
    if (url_size <= 0 || url_size >= sizeof(url)) {
        fprintf(stderr, "Failed to generate runtime URL\n");
        curl_global_cleanup();
        return false;
    }

    fprintf(stderr, "Downloading runtime file from %s\n", url);

    char curl_error_buf[CURL_ERROR_SIZE];
    CURL* handle = NULL;
    // should also be plenty big for the redirect target
    char effective_url[sizeof(url)];
    int success = -1L;

    curl_off_t content_length = -1;

    // first, we perform a HEAD request to determine the required buffer size to write the file to
    // of course, this assumes that a) GitHub sends a Content-Length header and b) that it is correct and will be in
    // the GET request, too
    // we store the URL we are redirected to (which probably lies on some AWS and is unique to that file) for use in
    // the GET request, which should ensure we really download the file whose size we check now
    handle = curl_easy_init();
    
    if (handle == NULL) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        curl_global_cleanup();
        return false;
    }
    
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    // should be plenty for GitHub
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 12L);
    curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error_buf);
    if (verbose) {
        curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
    }

    success = curl_easy_perform(handle);

    if (success == CURLE_OK) {
        // we want to clean up the handle before we can use effective_url
        char* temp;
        if (curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &temp) == CURLE_OK) {
            strncpy(effective_url, temp, sizeof(effective_url) - 1);
            // just a little sanity check
            if (strlen(effective_url) != strlen(temp)) {
                fprintf(stderr, "Failed to copy effective URL\n");
                // delegate cleanup
                effective_url[0] = '\0';
            } else if (strcmp(url, effective_url) != 0) {
                fprintf(stderr, "Redirected to %s\n", effective_url);
            }
        } else {
            fprintf(stderr, "Error: failed to determine effective URL\n");
            // we recycle the cleanup call below and check whether effective_url was set to anything meaningful below
        }

        if (curl_easy_getinfo(handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length) == CURLE_OK) {
            fprintf(stderr, "Downloading runtime binary of size %" CURL_FORMAT_CURL_OFF_T "\n", content_length);
        } else {
            fprintf(stderr, "Error: no Content-Length header sent by GitHub\n");
            // we recycle the cleanup call below and check whether content_length was set to anything meaningful below
        }
    } else {
        fprintf(stderr, "HEAD request to %s failed: %s\n", url, curl_error_buf);
    }

    curl_easy_cleanup(handle);
    handle = NULL;

    if (success != CURLE_OK || strlen(effective_url) == 0 || content_length <= 0) {
        curl_global_cleanup();
        return false;
    }

    // now that we know the required buffer size, we allocate a suitable in-memory buffer and perform the GET request
    // we allocate our own so that we don't have to use fread(...) to get the data
    char raw_buffer[content_length];
    FILE* file_buffer = fmemopen(raw_buffer, sizeof(raw_buffer), "w+b");
    setbuf(file_buffer, NULL);

    if (file_buffer == NULL) {
        fprintf(stderr, "fmemopen failed: %s\n", strerror(errno));
        curl_global_cleanup();
        return false;
    }

    handle = curl_easy_init();

    if (handle == NULL) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        curl_global_cleanup();
        return false;
    }

    // note: we should not need any more redirects
    curl_easy_setopt(handle, CURLOPT_URL, effective_url);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void*) file_buffer);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error_buf);
    if (verbose) {
        curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
    }

    success = curl_easy_perform(handle);

    int get_content_length;

    if (success != CURLE_OK) {
        fprintf(stderr, "GET request to %s failed: %s\n", effective_url, curl_error_buf);
        curl_easy_cleanup(handle);
        curl_global_cleanup();
        return false;
    } else {
        if (curl_easy_getinfo(handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &get_content_length) != CURLE_OK) {
            fprintf(stderr, "Error: no Content-Length header sent by GitHub\n");
            // we recycle the cleanup call below and check whether content_length was set to anything meaningful below
        }
    }

    curl_easy_cleanup(handle);
    handle = NULL;

    // done with libcurl
    curl_global_cleanup();

    if (get_content_length != content_length) {
        fprintf(stderr, "Downloading runtime binary of size %" CURL_FORMAT_CURL_OFF_T "\n", content_length);
    }

    *size = content_length;

    *buffer = (char*) calloc(content_length + 1, 1);

    if (*buffer == NULL) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return false;
    }

    memcpy((void* ) *buffer, raw_buffer, sizeof(raw_buffer));

    return true;
}
