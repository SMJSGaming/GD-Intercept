#include "HttpInfo.hpp"

std::vector<HttpInfo*> HttpInfo::requests;

FormToJson HttpInfo::formToJson;

RobTopToJson HttpInfo::robtopToJson;

BinaryToRaw HttpInfo::binaryToRaw;

HttpInfo* HttpInfo::create(CCHttpRequest* request) {
    HttpInfo* requestInfo = new HttpInfo(request);

    requestInfo->retain();

    if (!Mod::get()->getSettingValue<bool>("remember-requests") && HttpInfo::requests.size() > 0) {
        delete HttpInfo::requests.at(0);

        HttpInfo::requests.resize(0);
    }

    HttpInfo::requests.insert(HttpInfo::requests.begin(), requestInfo);

    return requestInfo;
}

HttpInfo::HttpInfo(CCHttpRequest* request) : m_active(false),
    m_method(request->getRequestType()),
    m_query(json::object()),
    m_headers(json::object()),
    m_responseCode(102),
    m_responseContentType(ContentType::UNKNOWN_CONTENT),
    m_originalTarget(request->getTarget()),
    m_originalProxy(request->getSelector()) {
    const char* body = request->getRequestData();
    const std::string url(request->getUrl());
    const size_t protocolEnd = url.find("://");
    const size_t queryStart = url.find('?');
    size_t pathStart;

    this->resetCache();

    if (protocolEnd == std::string::npos) {
        pathStart = url.find('/');
        m_protocol = Protocol::UNKNOWN_PROTOCOL;
        m_host = url.substr(0, pathStart == std::string::npos ? queryStart : pathStart);
    } else {
        const std::string protocol(url.substr(0, protocolEnd));

        if (protocol == "https") {
            m_protocol = Protocol::HTTPS;
        } else if (protocol == "http") {
            m_protocol = Protocol::HTTP;
        } else {
            m_protocol = Protocol::UNKNOWN_PROTOCOL;
        }

        pathStart = url.find('/', protocolEnd + 3);
        m_host = url.substr(protocolEnd + 3, (pathStart == std::string::npos ? queryStart : pathStart) - protocolEnd - 3);
    }

    // CCHttpRequest headers technically allow for weird header formats, but I'm assuming they're all key-value pairs separated by a colon since this is the standard
    // If you don't follow the standard, I'm not going to care that you get shitty results
    for (gd::string header : request->getHeaders()) {
        const std::string headerStr(header.c_str());
        const size_t colonPos = headerStr.find(":");

        if (colonPos == std::string::npos) {
            m_headers[headerStr] = json();
        } else {
            std::string value(headerStr.substr(colonPos + 1));

            m_headers[headerStr.substr(0, colonPos)] = json(value.erase(0, value.find_first_not_of(' ')));
        }
    }

    if (pathStart == std::string::npos) {
        m_path = "/";
    } else {
        m_path = url.substr(pathStart, queryStart == std::string::npos ? std::string::npos : queryStart - pathStart);
    }

    if (queryStart != std::string::npos) {
        m_query = HttpInfo::formToJson.convert(m_path, url.substr(queryStart + 1));
    }

    m_body = body ? std::string(request->getRequestData()).substr(0, request->getRequestDataSize()) : "";
    m_bodyContentType = this->determineContentType(m_body, true);
    request->setResponseCallback(this, httpresponse_selector(HttpInfo::onResponse));
}

bool HttpInfo::isActive() {
    return m_active;
}

void HttpInfo::setActive(const bool active) {
    m_active = active;
}

std::string HttpInfo::getHost() {
    return m_host;
}

std::string HttpInfo::getPath() {
    return m_path;
}

HttpInfo::ContentType HttpInfo::getBodyContentType() {
    return m_bodyContentType;
}

HttpInfo::ContentType HttpInfo::getResponseContentType() {
    return m_responseContentType;
}

std::string HttpInfo::formatProtocol() {
    switch (m_protocol) {
        case Protocol::HTTP: return "HTTP";
        case Protocol::HTTPS: return "HTTPS";
        default: return "UNKNOWN";
    }
}

std::string HttpInfo::formatMethod() {
    switch (m_method) {
        case CCHttpRequest::kHttpGet: return "GET";
        case CCHttpRequest::kHttpPost: return "POST";
        case CCHttpRequest::kHttpPut: return "PUT";
        case CCHttpRequest::kHttpDelete: return "DELETE";
        default: return "UNKNOWN";
    }
}

std::string HttpInfo::formatQuery() {
    return m_query.dump(2);
}

std::string HttpInfo::formatHeaders() {
    return m_headers.dump(2);
}

HttpInfo::content HttpInfo::formatBody() {
    return this->getContent(m_bodyContentType, m_body, m_simplifiedBodyCache);
}

HttpInfo::content HttpInfo::formatResponse() {
    return this->getContent(m_responseContentType, m_response, m_simplifiedResponseCache);
}

unsigned int HttpInfo::getResponseCode() {
    return m_responseCode;
}

std::string HttpInfo::generateBasicInfo(const bool withStatus) {
    const std::string info(fmt::format("Method: {}\nProtocol: {}\nHost: {}\nPath: {}",
        this->formatMethod(),
        this->formatProtocol(),
        m_host,
        m_path));

    if (withStatus) {
        return fmt::format("Status Code: {}\n{}", m_responseCode, info);
    } else {
        return info;
    }
}

ccColor3B HttpInfo::colorForMethod() {
    switch (m_method) {
        case CCHttpRequest::kHttpGet: return { 0xA8, 0x96, 0xFF };
        case CCHttpRequest::kHttpPost: return { 0x7E, 0xCF, 0x2B };
        case CCHttpRequest::kHttpPut: return { 0xFF, 0x9A, 0x1F };
        case CCHttpRequest::kHttpDelete: return { 0xFF, 0x56, 0x31 };
        default: return { 0x46, 0xC1, 0xE6 };
    }
}

void HttpInfo::resetCache() {
    m_simplifiedBodyCache = { ContentType::UNKNOWN_CONTENT, "" };
    m_simplifiedResponseCache = { ContentType::UNKNOWN_CONTENT, "" };
}

HttpInfo::content HttpInfo::getContent(const ContentType originalContentType, const std::string& original, HttpInfo::content& cache) {
    if (Mod::get()->getSettingValue<bool>("raw-data")) {
        return { cache.first, original };
    } else if (cache.second.empty()) {
        const content simplified = this->simplifyContent({ originalContentType, original });

        if (Mod::get()->getSettingValue<bool>("cache")) {
            cache = simplified;
        }

        return simplified;
    } else {
        return cache;
    }
}

HttpInfo::content HttpInfo::simplifyContent(const HttpInfo::content& content) {
    switch (content.first) {
        case ContentType::FORM: return { ContentType::JSON, HttpInfo::formToJson.convert(m_path, content.second).dump(2) };
        case ContentType::ROBTOP: return { ContentType::JSON, HttpInfo::robtopToJson.convert(m_path, content.second).dump(2) };
        case ContentType::BINARY: return { ContentType::BINARY, HttpInfo::binaryToRaw.convert(content.second) };
        default: return content;
    }
}

HttpInfo::ContentType HttpInfo::determineContentType(const std::string& content, const bool isBody) {
    if (content.empty()) {
        return ContentType::UNKNOWN_CONTENT;
    } else if (HttpInfo::binaryToRaw.canConvert(content)) {
        return ContentType::BINARY;
    } else if (JsonConverter::isJson(content)) {
        return ContentType::JSON;
    } else if (content.starts_with('<')) {
        return ContentType::XML;
    } else if (!isBody && HttpInfo::robtopToJson.canConvert(m_path, content)) {
        return ContentType::ROBTOP;
    } else if (HttpInfo::formToJson.canConvert(m_path, content)) {
        return ContentType::FORM;
    } else {
        return ContentType::UNKNOWN_CONTENT;
    }
}

void HttpInfo::onResponse(CCHttpClient* client, CCHttpResponse* response) {
    gd::vector<char>* data = response->getResponseData();

    m_response = std::string(data->begin(), data->end());
    m_responseCode = response->getResponseCode();
    m_responseContentType = this->determineContentType(m_response);

    (m_originalTarget->*m_originalProxy)(client, response);
}