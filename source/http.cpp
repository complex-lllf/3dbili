#include "http.h"
#include "log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

// 默认请求头
static std::string s_userAgent = "Mozilla/5.0 (Nintendo 3DS; Linux) AppleWebKit/605.1.15";
static std::string s_referer   = "https://www.bilibili.com";
static bool s_httpInitialized  = false;

bool Http_Init() {
    if (s_httpInitialized) return true;
    // GET 请求不需要 sharedmem，传 0 即可
    Result rc = httpcInit(0);
    if (R_FAILED(rc)) {
        LOGF("httpcInit failed: 0x%08lX\n", (unsigned long)rc);
        return false;
    }
    s_httpInitialized = true;
    LOGF("httpc initialized\n");
    return true;
}

void Http_Exit() {
    if (s_httpInitialized) {
        httpcExit();
        s_httpInitialized = false;
        LOGF("httpc exited\n");
    }
}

void Http_SetHeaders(const std::string& userAgent, const std::string& referer) {
    s_userAgent = userAgent;
    s_referer   = referer;
    LOGF("headers set: UA=%s REF=%s\n", userAgent.c_str(), referer.c_str());
}

// 内部：构建并发送请求
static Result Http_DoRequest(const std::string& url,
                             httpcContext* ctx,
                             u32* outStatus,
                             const std::vector<std::string>& extraHeaders) {
    Result rc;

    LOGF("GET %s\n", url.c_str());

    rc = httpcOpenContext(ctx, HTTPC_METHOD_GET, url.c_str(), 1);
    if (R_FAILED(rc)) {
        LOGF("  httpcOpenContext failed: 0x%08lX\n", (unsigned long)rc);
        return rc;
    }

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
    if (R_FAILED(rc)) {
        LOGF("  httpcBeginRequest failed: 0x%08lX\n", (unsigned long)rc);
        return rc;
    }

    rc = httpcGetResponseStatusCode(ctx, outStatus);
    LOGF("  HTTP status: %lu\n", (unsigned long)*outStatus);
    return rc;
}

// 内部：获取内容总大小
static size_t Http_GetContentLength(httpcContext* ctx) {
    u32 downloadSize = 0;
    u32 contentSize  = 0;
    Result rc = httpcGetDownloadSizeState(ctx, &downloadSize, &contentSize);
    if (R_FAILED(rc)) {
        LOGF("  httpcGetDownloadSizeState failed: 0x%08lX\n", (unsigned long)rc);
        return 0;
    }
    LOGF("  content size: %lu\n", (unsigned long)contentSize);
    return (size_t)contentSize;
}

HttpResponse Http_Get(const std::string& url, const std::vector<std::string>& extraHeaders) {
    HttpResponse resp = { false, 0, "", nullptr, 0 };

    if (!s_httpInitialized) {
        LOGF("Http_Get called but httpc not initialized\n");
        return resp;
    }

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
        if (total == 0 || total > 8 * 1024 * 1024) {
            LOGF("  abnormal content length: %lu, aborting\n", (unsigned long)total);
            httpcCloseContext(&ctx);
            return resp;
        }

        resp.body.resize(total);
        u32 downloaded = 0;
        rc = httpcDownloadData(&ctx, (u8*)resp.body.data(), (u32)total, &downloaded);
        if (R_SUCCEEDED(rc) && downloaded > 0) {
            resp.body.resize(downloaded);
            resp.success = true;
            LOGF("  body read: %u bytes\n", downloaded);
        } else {
            LOGF("  httpcDownloadData failed: 0x%08lX\n", (unsigned long)rc);
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
    if (total == 0 || total > 4 * 1024 * 1024) {
        LOGF("  abnormal content length: %lu, aborting\n", (unsigned long)total);
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
    if (R_SUCCEEDED(rc) && downloaded > 0) {
        resp.binarySize = downloaded;
        resp.success = true;
        LOGF("  downloaded %u bytes to memory\n", downloaded);
    } else {
        free(resp.binaryData);
        resp.binaryData = nullptr;
        LOGF("  httpcDownloadData (memory) failed: 0x%08lX\n", (unsigned long)rc);
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
        LOGF("  request failed before download, status=%lu\n", (unsigned long)status);
        httpcCloseContext(&ctx);
        return resp;
    }

    resp.statusCode = (int)status;

    size_t totalSize = Http_GetContentLength(&ctx);

    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        LOGF("  cannot open %s for writing\n", filePath.c_str());
        httpcCloseContext(&ctx);
        return resp;
    }

    const u32 CHUNK_SIZE = 16384;
    u8* buffer = (u8*)malloc(CHUNK_SIZE);
    if (!buffer) {
        fclose(fp);
        httpcCloseContext(&ctx);
        return resp;
    }

    size_t downloaded = 0;
    bool finished = false;
    Result lastRc = 0;
    bool hadError = false;

    // 如果 totalSize 为 0，则读到没有数据为止
    while (!finished) {
        u32 chunk = CHUNK_SIZE;
        if (totalSize > 0) {
            if (downloaded >= totalSize) break;
            size_t remaining = totalSize - downloaded;
            if (remaining < CHUNK_SIZE) chunk = (u32)remaining;
        }

        u32 readBytes = 0;
        rc = httpcDownloadData(&ctx, buffer, chunk, &readBytes);
        // 【修复】区分传输失败与流结束
        if (R_FAILED(rc)) {
            lastRc = rc;
            hadError = true;
            // 【修复】先取消连接，再关闭上下文，防止底层死锁或异常
            httpcCancelConnection(&ctx);
            LOGF("  httpcDownloadData failed: 0x%08lX\n", (unsigned long)rc);
            finished = true;
            break;
        }
        if (readBytes == 0) {
            finished = true;
            break;
        }

        // 【修复】检查 fwrite 短写
        size_t written = fwrite(buffer, 1, readBytes, fp);
        if (written != readBytes) {
            LOGF("  fwrite short write: %zu/%u\n", written, readBytes);
            hadError = true;
            finished = true;
            break;
        }
        downloaded += readBytes;

        if (progressCallback) progressCallback(downloaded, totalSize);

        if (readBytes < chunk) {
            // 没有更多数据
            finished = true;
        }
    }

    free(buffer);
    fclose(fp);
    httpcCloseContext(&ctx);

    if (downloaded > 0 && !hadError) {
        resp.success = true;
        resp.binarySize = downloaded;
        LOGF("  file downloaded: %lu bytes -> %s\n",
             (unsigned long)downloaded, filePath.c_str());
    } else {
        LOGF("  no data downloaded for %s (rc=0x%08lX)\n",
             url.c_str(), (unsigned long)lastRc);
    }

    // 【修复】消除 warning：显式忽略未使用变量
    (void)lastRc;

    return resp;
}