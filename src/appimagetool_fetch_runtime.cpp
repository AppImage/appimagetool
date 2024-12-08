#include <string>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>
#include <array>
#include <filesystem>
#include <algorithm>

#include <curl/curl.h>

#include "appimagetool_fetch_runtime.h"

class CurlResponse {
private:
    bool _success;
    long _statusCode;
    std::string _effectiveUrl;
    curl_off_t _contentLength;
    std::vector<char> _data;

public:
    CurlResponse(bool success, long statusCode, curl_off_t contentLength, std::vector<char> data)
            : _success(success)
            , _statusCode(statusCode)
            , _contentLength(contentLength)
            , _data(std::move(data)) {}

    [[nodiscard]] bool success() const {
        return _success;
    }

    [[nodiscard]] auto statusCode() const {
        return _statusCode;
    }

    [[nodiscard]] curl_off_t contentLength() const {
        return _contentLength;
    }

    [[nodiscard]] const std::vector<char>& data() const {
        return _data;
    }
};

std::string findCaBundleFile() {
    constexpr std::array locations = {
        // Debian/Ubuntu/Gentoo etc.
        "/etc/ssl/certs/ca-certificates.crt",
        // Fedora/RHEL 6
        "/etc/pki/tls/certs/ca-bundle.crt",
        // OpenSUSE
        "/etc/ssl/ca-bundle.pem",
        // OpenELEC
        "/etc/pki/tls/cacert.pem",
        // CentOS/RHEL 7
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        // Alpine Linux
        "/etc/ssl/cert.pem",
    };

    auto found = std::find_if(locations.begin(), locations.end(), [](const auto& path) {
        return std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path);
    });

    if (found != locations.end()) {
        return *found;
    }

    return {};
}

std::string findCaBundleDirectory() {
    constexpr std::array locations = {
        // SLES10/SLES11, https://golang.org/issue/12139
        "/etc/ssl/certs",
        // Fedora/RHEL
        "/etc/pki/tls/certs",
        // Android
        "/system/etc/security/cacerts",
    };

    auto found = std::find_if(locations.begin(), locations.end(), [](const auto& path) {
        return std::filesystem::is_directory(path);
    });

    if (found != locations.end()) {
        return *found;
    }

    return {};
}

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

    /**
     * Query data from CURL* handle.
     * @tparam T name of the type curl_easy_getinfo will return for the given option
     * @param option option to query
     * @return value returned by libcurl
     */
    template<typename T>
    T getOption(CURLINFO option) {
        T temp;
        checkForCurlError(curl_easy_getinfo(_handle, option, &temp));
        return temp;
    }

    /**
     * Set options within CURL* handle.
     * @tparam T name of the input variable's type (usually deduced automatically, you do not have to pass this
     *     explicitly)
     * @param option option to set using curl_easy_setinfo
     * @param value value to set
     */
    template<typename T>
    void setOption(CURLoption option, T value) {
        checkForCurlError(curl_easy_setopt(_handle, option, value));
    }

    void setUpTlsCaChainCompatibility(bool verbose) {
        bool foundFile = false;
        bool foundDir = false;

        // from curl 7.84.0 on, one can query the default values and check if these files or directories exist
        // if not, we anyway run the detection
#define querying_supported LIBCURL_VERSION_NUM >= CURL_VERSION_BITS(7, 84, 0)
#if querying_supported
        {
            const auto caInfo = getOption<char*>(CURLINFO_CAINFO);
            if (caInfo != nullptr && std::filesystem::exists(caInfo)) {
                if (verbose) {
                    std::cerr << "libcurl's default CA certificate bundle file " << caInfo << " was found on this system" << std::endl;
                }
                foundFile = true;
            } else {
                if (verbose) {
                    std::cerr << "libcurl's default CA certificate bundle file " << caInfo << " was not found on this system, nulling" << std::endl;
                }
                setOption(CURLOPT_CAINFO, "");
            }
        }

        {
            const auto caPath = getOption<char*>(CURLINFO_CAPATH);
            if (caPath != nullptr && std::filesystem::is_directory(caPath)) {
                if (verbose) {
                    std::cerr << "libcurl's default CA certificate bundle directory " << caPath
                              << " was found on this system" << std::endl;
                }
                foundDir = true;
            } else {
                if (verbose) {
                    std::cerr << "libcurl's default CA certificate bundle directory " << caPath << " was not found on this system, nulling" << std::endl;
                }
                setOption(CURLOPT_CAPATH, "");
            }
        }
#else
#warning "libcurl version too old, not trying to use default values for system-provided CA certificate bundles"
#endif

        if (!foundFile) {
            const auto chainFile = findCaBundleFile();
            if (!chainFile.empty()) {
                if (verbose) {
                    std::cerr << "Using CA bundle file in " << chainFile << std::endl;
                }
                setOption(CURLOPT_CAINFO, chainFile.c_str());
            }
            foundFile = true;
        }

        if (!foundDir) {
            const auto chainDir = findCaBundleDirectory();
            if (!chainDir.empty()) {
                if (verbose) {
                    std::cerr << "Using CA bundle dir in " << chainDir << std::endl;
                }
                setOption(CURLOPT_CAPATH, chainDir.c_str());
            }
            foundDir = true;
        }

        if (!foundFile && !foundDir) {
            std::cerr << "Warning: could not find valid CA chain bundle, HTTPS requests will likely fail" << std::endl;
        }
    }

public:
    explicit GetRequest(const std::string& url, bool verbose = false) : _errorBuffer(CURL_ERROR_SIZE) {
        // not the cleanest approach to globally init curl here, but this method shouldn't be called more than once anyway
        curl_global_init(CURL_GLOBAL_ALL);

        this->_handle = curl_easy_init();
        if (_handle == nullptr) {
            throw std::runtime_error("Failed to initialize libcurl");
        }

        setOption(CURLOPT_URL, url.c_str());

        // default parameters
        setOption(CURLOPT_FOLLOWLOCATION, 1L);
        setOption(CURLOPT_MAXREDIRS, 12L);

        if (verbose) {
            setOption(CURLOPT_VERBOSE, verbose ? 1L : 0L);
        }

        // support TLS CA chains on various distributions
        setUpTlsCaChainCompatibility(verbose);

        // needed to handle request internally
        setOption(CURLOPT_WRITEFUNCTION, GetRequest::writeStuff);
        setOption(CURLOPT_WRITEDATA, static_cast<void*>(this));
        setOption(CURLOPT_ERRORBUFFER, _errorBuffer.data());
    }

    GetRequest(const GetRequest&) = delete;
    GetRequest(GetRequest&&) = delete;

    ~GetRequest() {
        curl_global_cleanup();
        curl_easy_cleanup(this->_handle);
    }

    CurlResponse perform() {
        auto result = curl_easy_perform(this->_handle);
        return {
            result == CURLE_OK,
            getOption<long>(CURLINFO_RESPONSE_CODE),
            getOption<curl_off_t>(CURLINFO_CONTENT_LENGTH_DOWNLOAD_T),
            _buffer
        };
    }
};

bool fetch_runtime(char *arch, size_t *size, char **buffer, bool verbose) {
    std::ostringstream urlstream;
    urlstream << "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-" << arch;
    auto url = urlstream.str();

    std::cerr << "Downloading runtime file from " << url << std::endl;

    try {
        GetRequest request(url, verbose);

        auto response = request.perform();

        if (response.statusCode() != 200) {
            std::cerr << "Failed to download runtime: server returned status code " << response.statusCode() << std::endl;
            return false;
        }

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
