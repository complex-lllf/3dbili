#pragma once

#include <3ds.h>
#include <string>
#include <vector>

// HTTP 请求封装模块
// 职责：封装 libctru httpc 服务，提供 GET 请求与文件下载能力
// 注意：3DS 原生 httpc 不支持 TLS 1.2/HTTPS，B站接口需使用 http:// 或通过代理转发

struct HttpResponse {
    bool success;
    int statusCode;
    std::string body;      // 文本响应体（用于 JSON 解析）
    u8* binaryData;        // 二进制数据（用于图片/视频）
    size_t binarySize;     // 二进制数据大小
};

// 全局 HTTP 服务初始化/退出
bool Http_Init();
void Http_Exit();

// 设置默认请求头（User-Agent / Referer）
void Http_SetHeaders(const std::string& userAgent, const std::string& referer);

// 发起 GET 请求，返回文本响应
HttpResponse Http_Get(const std::string& url, const std::vector<std::string>& extraHeaders = {});

// 发起 GET 请求，返回二进制数据（用于下载图片、视频）
HttpResponse Http_DownloadToFile(const std::string& url, const std::string& filePath,
                                  void (*progressCallback)(size_t current, size_t total) = nullptr);

// 下载到内存（用于封面缩略图）
HttpResponse Http_DownloadToMemory(const std::string& url);