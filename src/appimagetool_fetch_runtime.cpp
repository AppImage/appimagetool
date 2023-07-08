// need to define this to enable asprintf
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <iostream>
#include <sstream>

#include <curl/curl.h>
#include <malloc.h>

#include "appimagetool_fetch_runtime.h"

#include <vector>

enum class RequestType {
    GET,
    HEAD,
};

class CurlResponse {
private:
    bool _success;
    std::string _effectiveUrl;
    curl_off_t _contentLength;

public:
    CurlResponse(bool success, std::string effectiveUrl, curl_off_t contentLength) : _success(success), _effectiveUrl(effectiveUrl), _contentLength(contentLength) {}

    bool success() {
        return _success;
    }

    std::string effectiveUrl() {
        return _effectiveUrl;
    };

    curl_off_t contentLength() {
        return _contentLength;
    }
};

class CurlRequest {
private:
    CURL* _handle;

public:
    CurlRequest(std::string url, RequestType requestType) {
        // not the cleanest approach to globally init curl here, but this method shouldn't be called more than once anyway
        curl_global_init(CURL_GLOBAL_ALL);

        this->_handle = curl_easy_init();
        if (_handle == nullptr) {
            throw std::runtime_error("Failed to initialize libcurl\n");
        }

        curl_easy_setopt(this->_handle, CURLOPT_URL, url.c_str());

        switch (requestType) {
            case RequestType::GET:
                break;
            case RequestType::HEAD:
                curl_easy_setopt(this->_handle, CURLOPT_NOBODY, 1L);
                break;
        }

        // default parameters
        curl_easy_setopt(_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(_handle, CURLOPT_MAXREDIRS, 12L);

        // curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error_buf);
    }

    ~CurlRequest() {
        curl_global_cleanup();
        curl_easy_cleanup(this->_handle);
    }

    void setVerbose(bool verbose) {
        curl_easy_setopt(this->_handle, CURLOPT_VERBOSE, verbose ? 1L : 0L);
    }

    void setInsecure(bool insecure) {
        std::cerr << "Warning: insecure request, please be careful!" << std::endl;
        curl_easy_setopt(this->_handle, CURLOPT_SSL_VERIFYPEER, insecure ? 0L : 1L);
    }

    CurlResponse perform() {
        auto result = curl_easy_perform(this->_handle);

        char* effectiveUrl;
        curl_easy_getinfo(_handle, CURLINFO_EFFECTIVE_URL, &effectiveUrl);

        curl_off_t contentLength;
        curl_easy_getinfo(_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);

        return {result == CURLE_OK, effectiveUrl, contentLength};
    }

    void setFileBuffer(FILE* fileBuffer) {
        curl_easy_setopt(_handle, CURLOPT_WRITEDATA, (void*) fileBuffer);
//        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, (void*) fileBuffer);
    }
};

bool fetch_runtime(char *arch, size_t *size, char **buffer, bool verbose) {
    std::ostringstream urlstream;
    urlstream << "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-" << arch;
    auto url = urlstream.str();

    std::cerr << "Downloading runtime file from " << url << std::endl;

    // first, we perform a HEAD request to determine the required buffer size to write the file to
    // of course, this assumes that a) GitHub sends a Content-Length header and b) that it is correct and will be in
    // the GET request, too
    // we store the URL we are redirected to (which probably lies on some AWS and is unique to that file) for use in
    // the GET request, which should ensure we really download the file whose size we check now
    CurlRequest headRequest(url, RequestType::HEAD);

    if (verbose) {
        headRequest.setVerbose(true);
    }

    auto headResponse = headRequest.perform();

    if (!headResponse.success()) {
        return false;
    }

    if (headResponse.effectiveUrl() != url) {
        std::cerr << "Redirected to " << headResponse.effectiveUrl() << std::endl;
    }

    // now that we know the required buffer size, we allocate a suitable in-memory buffer and perform the GET request
    // we allocate our own so that we don't have to use fread(...) to get the data
    std::vector<char> rawBuffer(headResponse.contentLength());
    FILE *file_buffer = fmemopen(rawBuffer.data(), rawBuffer.size(), "w+b");
    setbuf(file_buffer, NULL);

    if (file_buffer == NULL) {
        std::cerr << "fmemopen failed " << strerror(errno) << std::endl;
        return false;
    }

    std::cerr << "Downloading runtime binary of size " << headResponse.contentLength() << std::endl;

    CurlRequest getRequest(headResponse.effectiveUrl(), RequestType::GET);

    if (verbose) {
        getRequest.setVerbose(true);
    }

    getRequest.setFileBuffer(file_buffer);

    auto getResponse = getRequest.perform();

    if (!getResponse.success()) {
        return false;
    }

    if (headResponse.contentLength() != getResponse.contentLength()) {
        std::cerr << "Error: content length does not match" << std::endl;
        return false;
    }

    *size = getResponse.contentLength();

    *buffer = (char *) calloc(getResponse.contentLength() + 1, 1);

    if (*buffer == NULL) {
        std::cerr << "Failed to allocate buffer" << std::endl;
        return false;
    }

    std::copy(rawBuffer.begin(), rawBuffer.end(), *buffer);

    return true;
}
