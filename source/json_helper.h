#pragma once

#include <string>
#include <vector>
#include <functional>

// JSON 解析辅助模块
// 职责：封装 jsmn 库，提供简易的 token 遍历与字段提取能力

struct JsonToken {
    int start;
    int end;
    int size;
    int parent;
    int type;
};

class JsonParser {
public:
    int Parse(const std::string& json);

    int FindKey(const std::string& json, const std::string& key, int startToken = 0, int endToken = -1);

    std::string GetString(const std::string& json, int tokenIndex);
    long GetInt(const std::string& json, int tokenIndex);

    int ArrayGet(const std::string& json, int arrayToken, int index);
    int ObjectGetKey(const std::string& json, int objectToken, int index);
    int ObjectGetValue(const std::string& json, int objectToken, int index);

    bool IsString(int tokenIndex) const;
    bool IsArray(int tokenIndex) const;
    bool IsObject(int tokenIndex) const;

    int GetSize(int tokenIndex) const;

    const JsonToken& GetToken(int index) const { return m_tokens[index]; }
    int GetTokenCount() const { return m_tokenCount; }

private:
    std::vector<JsonToken> m_tokens;
    int m_tokenCount = 0;
};

struct VideoSearchItem {
    std::string bvid;
    std::string title;
    std::string coverUrl;
    std::string author;
    long playCount;
};

std::vector<VideoSearchItem> ParseSearchResponse(const std::string& json);

bool ParseViewResponse(const std::string& json, long& outAid, long& outCid, std::string& outTitle);

std::string ParsePlayUrlResponse(const std::string& json);