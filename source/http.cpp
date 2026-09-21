#include "http.h"
#include <cstring>
#include <cstdio>

// 默认请求头
static std::string s_userAgent = "Mozilla/5.0 (Nintendo 3DS; Linux) AppleWebKit/605.1.15";
static std::string s_referer   = "https://www.bilibili.com";
static bool s_httpInitialized  = false;

bool Http_Init() {
    if (s_httpInitialized) return true;
    Result rc = httpcInit(0x10000); // 64KB 缓冲区
    if (R_FAILED(rc)) return false;
    s_httpInitialized = true;
    return true;
}

void Http_Exit() {
    if (s_httpInitialized) {
        httpcExit();
        s_httpInitialized = false;
    }
}

void Http_SetHeaders(const std::string& userAgent, const std::string& referer) {
    s_userAgent = userAgent;
    s_referer   = referer;
}

// 内部：构建并发送请求，返回 HTTP 上下文
static Result Http_DoRequest(const std::string& url,
                             httpcContext* ctx,
                             u32* outStatus,
                             const std::vector<std::string>& extraHeaders) {
    Result rc;

    rc = httpcOpenContext(ctx, HTTPC_METHOD_GET, url.c_str(), 1);
    if (R_FAILED(rc)) return rc;

    // 设置默认请求头
    httpcAddRequestHeaderField(ctx, "User-Agent", s_userAgent.c_str());
    httpcAddRequestHeaderField(ctx, "Referer", s_referer.c_str());
    httpcAddRequestHeaderField(ctx, "Accept", "application/json, text/plain, */*");

    // 追加自定义请求头
    for (const auto& h : extraHeaders) {
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string key = h.substr(0, colon);
            std::string val = h.substr(colon + 1);
            while (!val.empty() && val[0] == ' ') val.erase(0, 1);
            httpcAddRequestHeaderField(ctx, key.c_str(), val.c_str());
        }
    }

    // 对于视频流，允许重定向
    httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_DISABLED);
    rc = httpcBeginRequest(ctx);
    if (R_FAILED(rc)) return rc;

    rc = httpcGetResponseStatusCode(ctx, outStatus);
    return rc;
}

HttpResponse Http_Get(const std::string& url, const std::vector<std::string>& extraHeaders) {
    HttpResponse resp = { false, 0, "", nullptr, 0 };

    if (!s_httpInitialized) return resp;

    httpcContext ctx;
    u32 status = 0;
    Result rc = Http_DoRequest(url, &ctx, &status, extraHeaders);

    if (R_FAILED(rc)) {
        httpcCloseContext(&ctx);
        return resp;
    }

    resp.statusCode = (int)status;

    if (status == 200) {
        // 逐块读取响应体
        u32 readSize = 0;
        char buffer[8192];
        while (R_SUCCEEDED(httpcReadData(&ctx, (u8*)buffer, sizeof(buffer) - 1, &readSize)) && readSize > 0) {
            buffer[readSize] = '\0';
            resp.body.append(buffer, readSize);
            readSize = 0;
        }
        resp.success = true;
    }

    httpcCloseContext(&ctx);
    return resp;
}

HttpResponse Http_DownloadToMemory(const std::string& url) {
    HttpResponse resp = { false, 0, "", nullptr, 0 };

    if (!s_httpInitialized) return resp;

    httpcContext ctx;
    u32 status = 0;
    Result rc = Http_DoRequest(url, &ctx, &status, {});

    if (R_FAILED(rc) || status != 200) {
        httpcCloseContext(&ctx);
        return resp;
    }

    resp.statusCode = (int)status;

    // 动态增长缓冲区
    size_t capacity = 65536;
    resp.binaryData = (u8*)malloc(capacity);
    if (!resp.binaryData) {
        httpcCloseContext(&ctx);
        return resp;
    }

    u32 readSize = 0;
    while (R_SUCCEEDED(httpcReadData(&ctx, resp.binaryData + resp.binarySize,
                                     capacity - resp.binarySize, &readSize)) && readSize > 0) {
        resp.binarySize += readSize;
        if (resp.binarySize >= capacity) {
            capacity *= 2;
            u8* newBuf = (u8*)realloc(resp.binaryData, capacity);
            if (!newBuf) break;
            resp.binaryData = newBuf;
        }
        readSize = 0;
    }

    resp.success = true;
    httpcCloseContext(&ctx);
    return resp;
}

HttpResponse Http_DownloadToFile(const std::string& url, const std::string& filePath,
                                 void (*progressCallback)(size_t, size_t)) {
    HttpResponse resp = { false, 0, "", nullptr, 0 };

    if (!s_httpInitialized) return resp;

    httpcContext ctx;
    u32 status = 0;
    Result rc = Http_DoRequest(url, &ctx, &status, {});

    if (R_FAILED(rc) || status != 200) {
        httpcCloseContext(&ctx);
        return resp;
    }

    resp.statusCode = (int)status;

    // 获取 Content-Length
    u32 contentLength = 0;
    httpcGetResponseHeader(ctx, "Content-Length", nullptr, 0, &contentLength);
    char lenBuf[32] = {0};
    httpcGetResponseHeader(ctx, "Content-Length", lenBuf, sizeof(lenBuf), nullptr);
    size_t totalSize = (size_t)strtoul(lenBuf, nullptr, 10);

    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        httpcCloseContext(&ctx);
        return resp;
    }

    u32 readSize = 0;
    u8 buffer[16384];
    size_t downloaded = 0;

    while (R_SUCCEEDED(httpcReadData(&ctx, buffer, sizeof(buffer), &readSize)) && readSize > 0) {
        fwrite(buffer, 1, readSize, fp);
        downloaded += readSize;
        if (progressCallback) {
            progressCallback(downloaded, totalSize);
        }
        readSize = 0;
    }

    fclose(fp);
    httpcCloseContext(&ctx);
    resp.success = true;
    resp.binarySize = downloaded;
    return resp;
}