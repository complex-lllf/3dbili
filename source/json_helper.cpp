#include "json_helper.h"
#include "jsmn.h"
#include "log.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

static const int MAX_TOKENS = 16384;

int JsonParser::Parse(const std::string& json) {
    jsmn_parser parser;
    jsmn_init(&parser);

    m_tokens.resize(MAX_TOKENS);
    // 【修复】清零 token 内存，防止 jsmn 在畸形 JSON 下读取未初始化字段
    // 参考：DCMTK Bug #1197 (CWE-908 Use of Uninitialized Resource)
    memset(m_tokens.data(), 0, MAX_TOKENS * sizeof(JsonToken));

    int r = jsmn_parse(&parser, json.c_str(), json.length(),
                       (jsmntok_t*)m_tokens.data(), MAX_TOKENS);

    if (r < 0) {
        LOGF("jsmn_parse failed: %d\n", r);
        m_tokenCount = 0;
        return -1;
    }

    m_tokenCount = r;
    LOGF("jsmn parsed %d tokens\n", r);
    return r;
}

// 【修复】跳过 token 及其所有子孙节点，返回下一个兄弟节点的索引
int JsonParser::SkipToken(int tokenIndex) const {
    if (tokenIndex < 0 || tokenIndex >= m_tokenCount) return m_tokenCount;
    int next = tokenIndex + 1;
    int size = m_tokens[tokenIndex].size;
    for (int i = 0; i < size; i++) {
        next = SkipToken(next);
    }
    return next;
}

// 【修复】FindKey 只遍历直接子节点，遇到嵌套节点整体跳过
int JsonParser::FindKey(const std::string& json, const std::string& key,
                        int startToken, int endToken) {
    if (endToken < 0 || endToken > m_tokenCount) endToken = m_tokenCount;

    int i = startToken;
    while (i < endToken) {
        if (m_tokens[i].type == JSMN_STRING) {
            std::string tokenKey = GetString(json, i);
            if (tokenKey == key) {
                if (i + 1 < endToken) return i + 1;
                return -1;
            }
            // 跳过 key 和它的 value
            int next = i + 1;
            if (next < endToken) {
                next = SkipToken(next);
            }
            i = next;
        } else {
            // 非 string 的 token，直接跳过其整个子树
            i = SkipToken(i);
        }
    }
    return -1;
}

std::string JsonParser::GetString(const std::string& json, int tokenIndex) {
    if (tokenIndex < 0 || tokenIndex >= m_tokenCount) return "";
    const auto& t = m_tokens[tokenIndex];
    if (t.type != JSMN_STRING && t.type != JSMN_PRIMITIVE) return "";
    if (t.start < 0 || t.end < t.start || (size_t)t.end > json.size()) return "";
    return json.substr(t.start, t.end - t.start);
}

long JsonParser::GetInt(const std::string& json, int tokenIndex) {
    return strtol(GetString(json, tokenIndex).c_str(), nullptr, 10);
}

int JsonParser::ArrayGet(const std::string& /*json*/, int arrayToken, int index) {
    if (arrayToken < 0 || arrayToken >= m_tokenCount) return -1;
    if (m_tokens[arrayToken].type != JSMN_ARRAY) return -1;
    if (index < 0) return -1;

    int current = arrayToken + 1;
    for (int i = 0; i < index && i < m_tokens[arrayToken].size; i++) {
        current = SkipToken(current);
    }
    if (current >= m_tokenCount) return -1;
    return current;
}

int JsonParser::ObjectGetKey(const std::string& /*json*/, int objectToken, int index) {
    if (objectToken < 0 || objectToken >= m_tokenCount) return -1;
    if (m_tokens[objectToken].type != JSMN_OBJECT) return -1;
    if (index < 0) return -1;

    int current = objectToken + 1;
    for (int i = 0; i < index; i++) {
        // 跳过 key
        current = SkipToken(current);
        // 跳过 value
        if (current < m_tokenCount) {
            current = SkipToken(current);
        }
    }
    return (current < m_tokenCount) ? current : -1;
}

int JsonParser::ObjectGetValue(const std::string& json, int objectToken, int index) {
    int keyIdx = ObjectGetKey(json, objectToken, index);
    if (keyIdx < 0) return -1;
    int valIdx = keyIdx + 1;
    if (valIdx >= m_tokenCount) return -1;
    return valIdx;
}

bool JsonParser::IsString(int tokenIndex) const {
    return tokenIndex >= 0 && tokenIndex < m_tokenCount &&
           m_tokens[tokenIndex].type == JSMN_STRING;
}

bool JsonParser::IsArray(int tokenIndex) const {
    return tokenIndex >= 0 && tokenIndex < m_tokenCount &&
           m_tokens[tokenIndex].type == JSMN_ARRAY;
}

bool JsonParser::IsObject(int tokenIndex) const {
    return tokenIndex >= 0 && tokenIndex < m_tokenCount &&
           m_tokens[tokenIndex].type == JSMN_OBJECT;
}

int JsonParser::GetSize(int tokenIndex) const {
    if (tokenIndex < 0 || tokenIndex >= m_tokenCount) return 0;
    return m_tokens[tokenIndex].size;
}

std::vector<VideoSearchItem> ParseSearchResponse(const std::string& json) {
    std::vector<VideoSearchItem> results;

    JsonParser parser;
    if (parser.Parse(json) < 0) return results;

    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) {
        LOGF("search response: no 'data' field\n");
        return results;
    }

    int resultIdx = parser.FindKey(json, "result", dataIdx);
    if (resultIdx < 0) {
        LOGF("search response: no 'result' field\n");
        return results;
    }

    if (!parser.IsArray(resultIdx)) {
        LOGF("search response: 'result' is not an array\n");
        return results;
    }

    int count = parser.GetSize(resultIdx);
    LOGF("search response: %d results\n", count);

    for (int i = 0; i < count; i++) {
        int itemIdx = parser.ArrayGet(json, resultIdx, i);
        if (itemIdx < 0) break;
        if (!parser.IsObject(itemIdx)) continue;

        // 【修复】限定查找范围为当前 item 对象，避免跨 item 匹配同名 key
        int itemEnd = itemIdx + 1 + parser.GetSize(itemIdx);

        VideoSearchItem item;
        int idx;

        idx = parser.FindKey(json, "bvid", itemIdx, itemEnd);
        if (idx >= 0) item.bvid = parser.GetString(json, idx);

        idx = parser.FindKey(json, "title", itemIdx, itemEnd);
        if (idx >= 0) {
            item.title = parser.GetString(json, idx);
            // 移除 HTML 标签
            size_t pos;
            while ((pos = item.title.find('<')) != std::string::npos) {
                size_t end = item.title.find('>', pos);
                if (end == std::string::npos) break;
                item.title.erase(pos, end - pos + 1);
            }
        }

        idx = parser.FindKey(json, "pic", itemIdx, itemEnd);
        if (idx >= 0) {
            item.coverUrl = parser.GetString(json, idx);
            if (item.coverUrl.size() > 1 && item.coverUrl[0] == '/' && item.coverUrl[1] == '/') {
                item.coverUrl = "http:" + item.coverUrl;
            }
            item.coverUrl += "@240w_160h_1e_1c.jpg";
        }

        idx = parser.FindKey(json, "author", itemIdx, itemEnd);
        if (idx >= 0) item.author = parser.GetString(json, idx);

        idx = parser.FindKey(json, "play", itemIdx, itemEnd);
        if (idx >= 0) item.playCount = parser.GetInt(json, idx);

        if (!item.bvid.empty() && !item.title.empty()) {
            results.push_back(item);
        }
    }

    return results;
}

bool ParseViewResponse(const std::string& json, long& outAid, long& outCid, std::string& outTitle) {
    JsonParser parser;
    if (parser.Parse(json) < 0) return false;

    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) return false;
    // 【修复】检查 data 是否为 null 或非对象
    if (!parser.IsObject(dataIdx)) {
        LOGF("view response: 'data' is null or not an object\n");
        return false;
    }

    int idx;

    idx = parser.FindKey(json, "aid", dataIdx);
    if (idx < 0) return false;
    outAid = parser.GetInt(json, idx);

    idx = parser.FindKey(json, "cid", dataIdx);
    if (idx < 0) return false;
    outCid = parser.GetInt(json, idx);

    idx = parser.FindKey(json, "title", dataIdx);
    if (idx >= 0) outTitle = parser.GetString(json, idx);

    LOGF("view response: aid=%ld cid=%ld\n", outAid, outCid);
    return (outAid > 0 && outCid > 0);
}

std::string ParsePlayUrlResponse(const std::string& json) {
    JsonParser parser;
    if (parser.Parse(json) < 0) return "";

    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) return "";
    // 【修复】检查 data 是否为 null
    if (!parser.IsObject(dataIdx)) {
        LOGF("playurl response: 'data' is null or not an object\n");
        return "";
    }

    // 【修复】优先尝试 dash 格式（B站新接口返回 dash）
    int dashIdx = parser.FindKey(json, "dash", dataIdx);
    if (dashIdx >= 0 && parser.IsObject(dashIdx)) {
        int videoArr = parser.FindKey(json, "video", dashIdx);
        if (videoArr >= 0 && parser.IsArray(videoArr) && parser.GetSize(videoArr) > 0) {
            int first = parser.ArrayGet(json, videoArr, 0);
            if (first >= 0 && parser.IsObject(first)) {
                int firstEnd = first + 1 + parser.GetSize(first);
                int urlIdx = parser.FindKey(json, "baseUrl", first, firstEnd);
                if (urlIdx >= 0) {
                    std::string url = parser.GetString(json, urlIdx);
                    if (!url.empty()) {
                        LOGF("playurl dash direct: %s\n", url.c_str());
                        return url;
                    }
                }
            }
        }
    }

    // 【修复】回退到 durl 格式（旧接口）
    int durlIdx = parser.FindKey(json, "durl", dataIdx);
    if (durlIdx < 0) {
        LOGF("playurl response: no 'durl' or 'dash' field\n");
        return "";
    }

    if (!parser.IsArray(durlIdx) || parser.GetSize(durlIdx) <= 0) return "";

    int firstIdx = parser.ArrayGet(json, durlIdx, 0);
    if (firstIdx < 0 || !parser.IsObject(firstIdx)) return "";

    int firstEnd = firstIdx + 1 + parser.GetSize(firstIdx);
    int urlIdx = parser.FindKey(json, "url", firstIdx, firstEnd);
    if (urlIdx < 0) return "";

    std::string url = parser.GetString(json, urlIdx);
    if (url.size() > 1 && url[0] == '/' && url[1] == '/') {
        url = "http:" + url;
    }

    LOGF("playurl durl direct: %s\n", url.c_str());
    return url;
}