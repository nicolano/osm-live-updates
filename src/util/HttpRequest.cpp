// Copyright 2024, University of Freiburg
// Authors: Nicolas von Trott <nicolasvontrott@gmail.com>.

// This file is part of osm-live-updates.
//
// osm-live-updates is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// osm-live-updates is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with osm-live-updates.  If not, see <https://www.gnu.org/licenses/>.

#include "util/HttpRequest.h"

#include <curl/curl.h>
#include <iostream>
#include <utility>
#include <vector>
#include <cstring>

namespace olu::util {

// _________________________________________________________________________________________________
static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    auto& mem = *static_cast<std::string*>(userp);
    mem.append(static_cast<char*>(contents), realsize);
    memset(contents, 0, realsize);
    return realsize;
}

// _________________________________________________________________________________________________
void setup_curl(CURL* curl_handle, std::string& data, const std::string& url)
{
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);

//    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
}

// _________________________________________________________________________________________________
HttpRequest::HttpRequest(const HttpMethod& method, const std::string& url) {
    _curl = curl_easy_init();
    _method = method;
    // Initialize response code with failed init
    _res = CURLcode::CURLE_FAILED_INIT;
    setup_curl(_curl, _data, url);
}

// _________________________________________________________________________________________________
HttpRequest::~HttpRequest() {
    curl_easy_cleanup(_curl);
    curl_slist_free_all(_chunk);
}

// _________________________________________________________________________________________________
void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    std::string header = key + ": " + value;
    _chunk = curl_slist_append(_chunk, header.c_str());
}

// _________________________________________________________________________________________________
void HttpRequest::addBody(std::string body) {
    _body = std::move(body);
}

// _________________________________________________________________________________________________
std::string HttpRequest::perform() {
    std::string response;
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _chunk);

    if (_method == POST) {
        curl_easy_setopt(_curl,CURLOPT_POST,1L);
        curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, _body.c_str());
        curl_easy_setopt(_curl, CURLOPT_POSTFIELDSIZE, _body.length());
    }

    if(_curl) {
        _res = curl_easy_perform(_curl);
        response = _data;
    } else {
        throw HttpRequestException("Failed to initialize CURL");
    }

    if (_res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(_res));
    }

    return response;
}

} // namespace olu::util
