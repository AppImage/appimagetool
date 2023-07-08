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
#include <cassert>

enum class RequestType {
    GET,
    HEAD,
};

class CurlResponse {
private:
    bool _success;
    std::string _effectiveUrl;
    curl_off_t _contentLength;
    std::vector<char> _data;

public:
    CurlResponse(bool success, curl_off_t contentLength, std::vector<char> data)
        : _success(success)
        , _contentLength(contentLength)
        , _data(data) {}

    bool success() {
        return _success;
    }

    curl_off_t contentLength() {
        return _contentLength;
    }

    std::vector<char> data() {
        return _data;
    }
};

class CurlRequest {
private:
    CURL* _handle;
    std::vector<char> _buffer;

    static size_t writeStuff(char* data, size_t size, size_t nmemb, void* this_ptr) {
        const auto bytes = size * nmemb;
        std::copy(data, data + bytes, std::back_inserter(static_cast<CurlRequest*>(this_ptr)->_buffer));
        return bytes;
    }

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

        curl_easy_setopt(_handle, CURLOPT_WRITEFUNCTION, CurlRequest::writeStuff);
        curl_easy_setopt(_handle, CURLOPT_WRITEDATA, static_cast<void*>(this));

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

        curl_off_t contentLength;
        curl_easy_getinfo(_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);

        return {result == CURLE_OK, contentLength, _buffer};
    }
};

bool fetch_runtime(char *arch, size_t *size, char **buffer, bool verbose) {
    std::ostringstream urlstream;
    urlstream << "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-" << arch;
    auto url = urlstream.str();

    std::cerr << "Downloading runtime file from " << url << std::endl;

    CurlRequest request(url, RequestType::GET);

    if (verbose) {
        request.setVerbose(true);
    }

    auto response = request.perform();

    std::cerr << "Downloaded runtime binary of size " << response.contentLength() << std::endl;

    if (!response.success()) {
        return false;
    }

    auto runtimeData = response.data();

    if (runtimeData.size() != response.contentLength()) {
        std::cerr << "Error: downloaded data size of " << runtimeData.size()
                  << " does not match content-length of " << response.contentLength() << std::endl;
        return false;
    }

    *size = response.contentLength();

    *buffer = (char *) calloc(response.contentLength() + 1, 1);

    if (*buffer == NULL) {
        std::cerr << "Failed to allocate buffer" << std::endl;
        return false;
    }

    std::copy(runtimeData.begin(), runtimeData.end(), *buffer);

    return true;
}
