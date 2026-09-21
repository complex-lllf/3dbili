#pragma once

#include <string>
#include <vector>
#include <functional>

// JSON 解析辅助模块
// 职责：封装 jsmn 库，提供简易的 token 遍历与字段提取能力

// 一个 JSON token 的视图，指向原始字符串中的片段
struct JsonToken {
    int start;
    int end;
    int size;       // 子 token 数量
    int parent;
    int type;
};

class JsonParser {
public:
    // 解析 JSON 字符串，返回 token 数量（-1 表示失败）
    int Parse(const std::string& json);

    // 根据 key 名查找 object 中的字段（从指定 token 索引起始搜索）
    // 返回匹配的 value token 索引，-1 表示未找到
    int FindKey(const std::string& json, const std::string& key, int startToken = 0, int endToken = -1);

    // 获取 token 对应的字符串值
    std::string GetString(const std::string& json, int tokenIndex);

    // 获取 token 对应的整数值
    long GetInt(const std::string& json, int tokenIndex);

    // 获取 array token 中的第 n 个元素 token 索引
    int ArrayGet(const std::string& json, int arrayToken, int index);

    // 获取 object token 的第 n 个 key token 索引（隔一个取一个）
    int ObjectGetKey(const std::string& json, int objectToken, int index);

    // 获取 object token 的第 n 个 value token 索引
    int ObjectGetValue(const std::string& json, int objectToken, int index);

    // 判断 token 类型
    bool IsString(int tokenIndex) const;
    bool IsArray(int tokenIndex) const;
    bool IsObject(int tokenIndex) const;

    // 获取子 token 数量
    int GetSize(int tokenIndex) const;

    // 原始 token 数组访问
    const JsonToken& GetToken(int index) const { return m_tokens[index]; }
    int GetTokenCount() const { return m_tokenCount; }

private:
    std::vector<JsonToken> m_tokens;
    int m_tokenCount = 0;
};

// 高层辅助：从 B站搜索响应中提取视频列表
struct VideoSearchItem {
    std::string bvid;
    std::string title;
    std::string coverUrl;
    std::string author;
    long playCount;
};

std::vector<VideoSearchItem> ParseSearchResponse(const std::string& json);

// 高层辅助：从 view 响应中提取 aid 和 cid
bool ParseViewResponse(const std::string& json, long& outAid, long& outCid, std::string& outTitle);

// 高层辅助：从 playurl 响应中提取 MP4 直链
std::string ParsePlayUrlResponse(const std::string& json);