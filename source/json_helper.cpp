#include "json_helper.h"
#include "jsmn.h"
#include <cstring>
#include <cstdlib>

// jsmn 最大 token 数量
static const int MAX_TOKENS = 16384;

int JsonParser::Parse(const std::string& json) {
    jsmn_parser parser;
    jsmn_init(&parser);

    m_tokens.resize(MAX_TOKENS);
    int r = jsmn_parse(&parser, json.c_str(), json.length(),
                       (jsmntok_t*)m_tokens.data(), MAX_TOKENS);

    if (r < 0) {
        m_tokenCount = 0;
        return -1;
    }

    m_tokenCount = r;
    return r;
}

int JsonParser::FindKey(const std::string& json, const std::string& key, int startToken, int endToken) {
    if (endToken < 0) endToken = m_tokenCount;

    for (int i = startToken; i < endToken; i++) {
        if (m_tokens[i].type == JSMN_STRING) {
            std::string tokenKey = GetString(json, i);
            if (tokenKey == key) {
                // 下一个 token 即为 value
                if (i + 1 < endToken) {
                    return i + 1;
                }
            }
        }
    }
    return -1;
}

std::string JsonParser::GetString(const std::string& json, int tokenIndex) {
    if (tokenIndex < 0 || tokenIndex >= m_tokenCount) return "";
    const auto& t = m_tokens[tokenIndex];
    if (t.type != JSMN_STRING && t.type != JSMN_PRIMITIVE) return "";
    return json.substr(t.start, t.end - t.start);
}

long JsonParser::GetInt(const std::string& json, int tokenIndex) {
    return strtol(GetString(json, tokenIndex).c_str(), nullptr, 10);
}

int JsonParser::ArrayGet(const std::string& json, int arrayToken, int index) {
    if (arrayToken < 0 || arrayToken >= m_tokenCount) return -1;
    if (m_tokens[arrayToken].type != JSMN_ARRAY) return -1;

    int current = arrayToken + 1;
    for (int i = 0; i < index && i < m_tokens[arrayToken].size; i++) {
        // 跳过当前元素的子树
        current += 1 + m_tokens[current].size;
    }
    if (current >= m_tokenCount) return -1;
    return current;
}

int JsonParser::ObjectGetKey(const std::string& json, int objectToken, int index) {
    if (objectToken < 0 || objectToken >= m_tokenCount) return -1;
    if (m_tokens[objectToken].type != JSMN_OBJECT) return -1;

    int current = objectToken + 1;
    for (int i = 0; i < index; i++) {
        // key 之后跟 value
        current += 1 + m_tokens[current].size; // 跳过 key
        if (current < m_tokenCount) {
            current += 1 + m_tokens[current].size; // 跳过 value
        }
    }
    return (current < m_tokenCount) ? current : -1;
}

int JsonParser::ObjectGetValue(const std::string& json, int objectToken, int index) {
    int keyIdx = ObjectGetKey(json, objectToken, index);
    if (keyIdx < 0) return -1;
    return keyIdx + 1;
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

// ==================== 高层解析函数 ====================

// B站搜索 API 返回结构：
// { "code": 0, "data": { "result": [ { "bvid": "...", "title": "...",
//   "pic": "//i0.hdslb.com/...", "author": "...", "play": 12345 }, ... ] } }
std::vector<VideoSearchItem> ParseSearchResponse(const std::string& json) {
    std::vector<VideoSearchItem> results;

    JsonParser parser;
    if (parser.Parse(json) < 0) return results;

    // 定位 data 对象
    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) return results;

    // 定位 result 数组
    int resultIdx = parser.FindKey(json, "result", dataIdx);
    if (resultIdx < 0) return results;

    int count = parser.GetSize(resultIdx);
    for (int i = 0; i < count; i++) {
        int itemIdx = parser.ArrayGet(json, resultIdx, i);
        if (itemIdx < 0) break;

        VideoSearchItem item;
        int idx;

        idx = parser.FindKey(json, "bvid", itemIdx);
        if (idx >= 0) item.bvid = parser.GetString(json, idx);

        idx = parser.FindKey(json, "title", itemIdx);
        if (idx >= 0) {
            item.title = parser.GetString(json, idx);
            // 去除 HTML 标签（B站标题可能带 <em> 高亮）
            size_t pos;
            while ((pos = item.title.find('<')) != std::string::npos) {
                size_t end = item.title.find('>', pos);
                if (end == std::string::npos) break;
                item.title.erase(pos, end - pos + 1);
            }
        }

        idx = parser.FindKey(json, "pic", itemIdx);
        if (idx >= 0) {
            item.coverUrl = parser.GetString(json, idx);
            // B站图片 URL 可能以 // 开头，补全为 http://
            if (item.coverUrl.size() > 1 && item.coverUrl[0] == '/' && item.coverUrl[1] == '/') {
                item.coverUrl = "http:" + item.coverUrl;
            }
            // 缩略图 URL 可以加参数缩小尺寸（减少下载量）
            item.coverUrl += "@240w_160h_1e_1c.jpg";
        }

        idx = parser.FindKey(json, "author", itemIdx);
        if (idx >= 0) item.author = parser.GetString(json, idx);

        idx = parser.FindKey(json, "play", itemIdx);
        if (idx >= 0) item.playCount = parser.GetInt(json, idx);

        if (!item.bvid.empty() && !item.title.empty()) {
            results.push_back(item);
        }
    }

    return results;
}

// B站 view API 返回结构：
// { "code": 0, "data": { "aid": 123, "cid": 456, "title": "..." } }
bool ParseViewResponse(const std::string& json, long& outAid, long& outCid, std::string& outTitle) {
    JsonParser parser;
    if (parser.Parse(json) < 0) return false;

    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) return false;

    int idx;

    idx = parser.FindKey(json, "aid", dataIdx);
    if (idx < 0) return false;
    outAid = parser.GetInt(json, idx);

    idx = parser.FindKey(json, "cid", dataIdx);
    if (idx < 0) return false;
    outCid = parser.GetInt(json, idx);

    idx = parser.FindKey(json, "title", dataIdx);
    if (idx >= 0) outTitle = parser.GetString(json, idx);

    return (outAid > 0 && outCid > 0);
}

// B站 playurl API (fnval=1 为 MP4) 返回结构：
// { "code": 0, "data": { "durl": [ { "url": "http://...mp4", "size": 123 } ] } }
std::string ParsePlayUrlResponse(const std::string& json) {
    JsonParser parser;
    if (parser.Parse(json) < 0) return "";

    int dataIdx = parser.FindKey(json, "data", 0);
    if (dataIdx < 0) return "";

    int durlIdx = parser.FindKey(json, "durl", dataIdx);
    if (durlIdx < 0) return "";

    int count = parser.GetSize(durlIdx);
    if (count <= 0) return "";

    int firstIdx = parser.ArrayGet(json, durlIdx, 0);
    if (firstIdx < 0) return "";

    int urlIdx = parser.FindKey(json, "url", firstIdx);
    if (urlIdx < 0) return "";

    std::string url = parser.GetString(json, urlIdx);

    // 将 // 开头的 URL 补全
    if (url.size() > 1 && url[0] == '/' && url[1] == '/') {
        url = "http:" + url;
    }

    return url;
}