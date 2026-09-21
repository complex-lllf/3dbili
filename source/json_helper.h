#pragma once

#include <string>
#include <vector>
#include <functional>
#include "jsmn.h"   // 直接复用 jsmntok_t，避免结构体布局不一致的隐式 bug

// JSON 解析辅助模块
// 职责：封装 jsmn 库，提供简易的 token 遍历与字段提取能力

// 直接使用 jsmn 的 token 类型，保证与 jsmn_parse 写入的内存布局一致
using JsonToken = jsmntok_t;

class JsonParser {
public:
    int Parse(const std::string& json);

    // 在 [startToken, endToken) 范围内查找 key。
    // 只遍历当前层级的直接子节点，遇到嵌套对象/数组会整体跳过，
    // 避免匹配到子对象内部的同名 key。
    int FindKey(const std::string& json, const std::string& key,
                int startToken = 0, int endToken = -1);

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
    // 跳过 token 及其所有子孙节点，返回下一个兄弟节点的索引
    // 【修复】增加 depth 参数，限制递归深度，防止栈溢出
    int SkipToken(int tokenIndex, int depth = 0) const;

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