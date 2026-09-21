#include "http.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

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

// 内部：构建并发送请求
static Result Http_DoRequest(const std::string& url,
                             httpcContext* ctx,
                             u32* outStatus,
                             const std::vector<std::string>& extraHeaders) {
    Result rc;

    rc = httpcOpenContext(ctx, HTTPC_METHOD_GET, url.c_str(), 1);
    if (R_FAILED(rc)) return rc;

    httpcAddRequestHeaderField(ctx, "User-Agent", s_userAgent.c_str());
    httpcAddRequestHeaderField(ctx, "Referer", s_referer.c_str());
    httpcAddRequestHeaderField(ctx, "Accept", "application/json, text/plain, */*");

    for (const auto& h : extraHeaders) {
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string key = h.substr(0, colon);
            std::string val = h.substr(colon + 1);
            while (!val.empty() && val[0] == ' ') val.erase(0, 1);
            httpcAddRequestHeaderField(ctx, key.c_str(), val.c_str());
        }
    }

    httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_DISABLED);
    rc = httpcBeginRequest(ctx);
    if (R_FAILED(rc)) return rc;

    rc = httpcGetResponseStatusCode(ctx, outStatus);
    return rc;
}

// 内部：获取内容总大小
static size_t Http_GetContentLength(httpcContext* ctx) {
    u32 contentSize = 0;
    if (R_FAILED(httpcGetDownloadSizeState(ctx, nullptr, &contentSize))) {
        return 0;
    }
    return (size_t)contentSize;
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
        size_t total = Http_GetContentLength(&ctx);
        if (total == 0) {
            // 如果服务器没给 Content-Length，用保守的兜底策略：
            // 逐步读取直到 httpcDownloadData 返回没有更多数据
            // 但为了简洁，此处直接返回失败
            httpcCloseContext(&ctx);
            return resp;
        }

        // 预分配缓冲区
        resp.body.resize(total);

        u32 downloaded = 0;
        // httpcDownloadData 会一次性下载全部内容
        rc = httpcDownloadData(&ctx, (u8*)resp.body.data(), (u32)total, &downloaded);
        if (R_SUCCEEDED(rc)) {
            resp.body.resize(downloaded);
            resp.success = true;
        } else {
            resp.body.clear();
        }
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

    size_t total = Http_GetContentLength(&ctx);
    if (total == 0) {
        httpcCloseContext(&ctx);
        return resp;
    }

    resp.binaryData = (u8*)malloc(total);
    if (!resp.binaryData) {
        httpcCloseContext(&ctx);
        return resp;
    }

    u32 downloaded = 0;
    rc = httpcDownloadData(&ctx, resp.binaryData, (u32)total, &downloaded);
    if (R_SUCCEEDED(rc)) {
        resp.binarySize = downloaded;
        resp.success = true;
    } else {
        free(resp.binaryData);
        resp.binaryData = nullptr;
    }

    httpcCloseContext(&ctx);
    return resp;
}

HttpResponse Http_DownloadToFile(const std::string& url, const std::string& filePath,
                                 HttpProgressCallback progressCallback) {
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

    size_t totalSize = Http_GetContentLength(&ctx);

    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        httpcCloseContext(&ctx);
        return resp;
    }

    // 分块下载，避免一次性占用过多内存
    const u32 CHUNK_SIZE = 16384;
    u8* buffer = (u8*)malloc(CHUNK_SIZE);
    if (!buffer) {
        fclose(fp);
        httpcCloseContext(&ctx);
        return resp;
    }

    size_t downloaded = 0;
    bool success = false;

    while (downloaded < totalSize) {
        u32 chunk = (totalSize - downloaded > CHUNK_SIZE) ? CHUNK_SIZE : (u32)(totalSize - downloaded);
        u32 readBytes = 0;

        rc = httpcDownloadData(&ctx, buffer, chunk, &readBytes);
        if (R_FAILED(rc) || readBytes == 0) {
            break;
        }

        fwrite(buffer, 1, readBytes, fp);
        downloaded += readBytes;

        if (progressCallback) {
            progressCallback(downloaded, totalSize);
        }

        if (readBytes < chunk) {
            // 没有更多数据
            break;
        }
    }

    free(buffer);
    fclose(fp);
    httpcCloseContext(&ctx);

    if (downloaded > 0) {
        resp.success = true;
        resp.binarySize = downloaded;
    }

    return resp;
}