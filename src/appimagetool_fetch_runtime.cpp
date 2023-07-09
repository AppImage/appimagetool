#include <string>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "appimagetool_fetch_runtime.h"

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
        , _data(std::move(data)) {}

    [[nodiscard]] bool success() const {
        return _success;
    }

    [[nodiscard]] curl_off_t contentLength() const {
        return _contentLength;
    }

    [[nodiscard]] const std::vector<char>& data() const {
        return _data;
    }
};

class CurlException : public std::runtime_error {
public:
    CurlException(CURLcode code, const std::string& errorMessage) : std::runtime_error(std::string(curl_easy_strerror(code)) + ": " + errorMessage) {}
};

class GetRequest {
private:
    CURL* _handle;
    std::vector<char> _buffer;
    std::vector<char> _errorBuffer;

    static size_t writeStuff(char* data, size_t size, size_t nmemb, void* this_ptr) {
        const auto bytes = size * nmemb;
        std::copy(data, data + bytes, std::back_inserter(static_cast<GetRequest*>(this_ptr)->_buffer));
        return bytes;
    }

    void checkForCurlError(CURLcode code) {
        if (code != CURLE_OK) {
            throw CurlException(code, _errorBuffer.data());
        }
    }

public:
    explicit GetRequest(const std::string& url) : _errorBuffer(CURL_ERROR_SIZE) {
        // not the cleanest approach to globally init curl here, but this method shouldn't be called more than once anyway
        curl_global_init(CURL_GLOBAL_ALL);

        this->_handle = curl_easy_init();
        if (_handle == nullptr) {
            throw std::runtime_error("Failed to initialize libcurl");
        }

        checkForCurlError(curl_easy_setopt(this->_handle, CURLOPT_URL, url.c_str()));

        // default parameters
        checkForCurlError(curl_easy_setopt(_handle, CURLOPT_FOLLOWLOCATION, 1L));
        checkForCurlError(curl_easy_setopt(_handle, CURLOPT_MAXREDIRS, 12L));

        checkForCurlError(curl_easy_setopt(_handle, CURLOPT_WRITEFUNCTION, GetRequest::writeStuff));
        checkForCurlError(curl_easy_setopt(_handle, CURLOPT_WRITEDATA, static_cast<void*>(this)));

        checkForCurlError(curl_easy_setopt(_handle, CURLOPT_ERRORBUFFER, _errorBuffer.data()));
    }

    ~GetRequest() {
        curl_global_cleanup();
        curl_easy_cleanup(this->_handle);
    }

    void setVerbose(bool verbose) {
        checkForCurlError(curl_easy_setopt(this->_handle, CURLOPT_VERBOSE, verbose ? 1L : 0L));
    }

    CurlResponse perform() {
        auto result = curl_easy_perform(this->_handle);

        curl_off_t contentLength;
        checkForCurlError(curl_easy_getinfo(_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength));

        return {result == CURLE_OK, contentLength, _buffer};
    }
};

bool fetch_runtime(char *arch, size_t *size, char **buffer, bool verbose) {
    std::ostringstream urlstream;
    urlstream << "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-" << arch;
    auto url = urlstream.str();

    std::cerr << "Downloading runtime file from " << url << std::endl;

    try {
        GetRequest request(url);

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

        if (*buffer == nullptr) {
            std::cerr << "Failed to allocate buffer" << std::endl;
            return false;
        }

        std::copy(runtimeData.begin(), runtimeData.end(), *buffer);

        return true;
    } catch (const CurlException& e) {
        std::cerr << "libcurl error: " << e.what() << std::endl;
        return false;
    }
}
