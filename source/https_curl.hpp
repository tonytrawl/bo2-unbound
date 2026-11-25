#pragma once

#include "cacert_pem_bin.h"
#include <curl/curl.h>
#include <string>

namespace unbound {

inline bool configureHttpsTrust(CURL* curl, std::string& error) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_blob certificateBundle{
        const_cast<unsigned char*>(cacert_pem_bin), static_cast<std::size_t>(cacert_pem_bin_size),
        CURL_BLOB_NOCOPY
    };
    if (curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &certificateBundle) != CURLE_OK) {
        error = "Could not configure the HTTPS certificate bundle";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    return true;
}

} // namespace unbound
