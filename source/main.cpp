#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cctype>

#include "http.h"
#include "json_helper.h"
#include "ui.h"
#include "download.h"
#include "log.h"

// ==================== B站 API 常量 ====================
// 注意：3DS 原生 httpc 不支持 TLS 1.2，B站 HTTPS 接口无法直接访问。
// 以下使用 HTTP 端点，或可通过自建代理转发。

static const char* SEARCH_API = "http://api.bilibili.com/x/web-interface/search/type"
                                "?search_type=video&keyword=%s&page=1";

static const char* VIEW_API = "http://api.bilibili.com/x/web-interface/view?bvid=%s";

static const char* PLAYURL_API = "http://api.bilibili.com/x/player/playurl"
                                 "?bvid=%s&cid=%ld&qn=16&fnval=1&platform=html5";

// ==================== URL 编码 ====================
std::string UrlEncode(const std::string& input) {
    std::string result;
    char buf[8];
    for (unsigned char c : input) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

// ==================== 全局状态 ====================
AppState g_appState = AppState::SEARCH;
std::string g_searchQuery;
std::vector<VideoSearchItem> g_searchResults;
std::vector<std::string> g_downloadedFiles;
std::string g_statusMessage;

bool DoSearch(const std::string& keyword) {
    if (keyword.empty()) return false;

    g_statusMessage = "Searching...";
    LOGF("DoSearch: %s\n", keyword.c_str());

    char url[1024];
    snprintf(url, sizeof(url), SEARCH_API, UrlEncode(keyword).c_str());

    HttpResponse resp = Http_Get(url);
    if (!resp.success || resp.body.empty()) {
        g_statusMessage = "Search failed. Check network.";
        LOGF("search failed, HTTP %d, body size %lu\n",
             resp.statusCode, (unsigned long)resp.body.size());
        return false;
    }

    g_searchResults = ParseSearchResponse(resp.body);

    if (g_searchResults.empty()) {
        g_statusMessage = "No results found.";
        return false;
    }

    g_statusMessage = "Found " + std::to_string(g_searchResults.size()) + " videos.";
    return true;
}

bool DownloadVideo(const VideoSearchItem& item) {
    auto& dm = DownloadManager::Instance();
    if (dm.IsDownloading()) {
        g_statusMessage = "Already downloading.";
        return false;
    }

    char viewUrl[512];
    snprintf(viewUrl, sizeof(viewUrl), VIEW_API, item.bvid.c_str());

    HttpResponse viewResp = Http_Get(viewUrl);
    if (!viewResp.success) {
        g_statusMessage = "Failed to get video info.";
        return false;
    }

    long aid = 0, cid = 0;
    std::string title;
    if (!ParseViewResponse(viewResp.body, aid, cid, title)) {
        g_statusMessage = "Failed to parse video info.";
        return false;
    }

    char playUrl[512];
    snprintf(playUrl, sizeof(playUrl), PLAYURL_API, item.bvid.c_str(), cid);

    HttpResponse playResp = Http_Get(playUrl);
    if (!playResp.success) {
        g_statusMessage = "Failed to get play URL.";
        return false;
    }

    std::string directUrl = ParsePlayUrlResponse(playResp.body);
    if (directUrl.empty()) {
        g_statusMessage = "No MP4 URL found (video may be segmented).";
        return false;
    }

    if (!dm.StartDownload(item.bvid, item.title, directUrl)) {
        g_statusMessage = "Failed to start download.";
        return false;
    }

    g_appState = AppState::DOWNLOADING;
    return true;
}

std::string OpenKeyboard(const std::string& initialText) {
    SwkbdState swkbd;
    char buffer[256] = {0};

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 255);
    swkbdSetHintText(&swkbd, "Enter search keyword...");
    swkbdSetInitialText(&swkbd, initialText.c_str());
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    SwkbdButton button = swkbdInputText(&swkbd, buffer, sizeof(buffer));

    if (button == SWKBD_BUTTON_CONFIRM) {
        return std::string(buffer);
    }
    return "";
}

int main(int argc, char** argv) {
    // CIA 环境下必须先初始化 fs 服务并挂载 SD 卡
    // 3dsx 环境下重复调用也是安全的
    fsInit();
    sdmcInit();

    romfsInit();
    gfxInitDefault();
    hidInit();

    LOG_INIT();
    LOGF("=== 3dbili starting ===\n");
#ifdef ENABLE_DEBUG_LOG
    LOGF("build type: DEBUG\n");
#else
    LOGF("build type: STABLE\n");
#endif

    if (!Http_Init()) {
        consoleInit(GFX_TOP, NULL);
        printf("Failed to initialize HTTP service.\n");
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }
        Http_Exit();
        hidExit();
        gfxExit();
        romfsExit();
        sdmcExit();
        fsExit();
        LOG_CLOSE();
        return 1;
    }

    Http_SetHeaders(
        "Mozilla/5.0 (Nintendo 3DS; Linux) AppleWebKit/605.1.15",
        "https://www.bilibili.com"
    );

    UIManager ui;
    if (!ui.Init()) {
        Http_Exit();
        hidExit();
        gfxExit();
        romfsExit();
        sdmcExit();
        fsExit();
        LOG_CLOSE();
        return 1;
    }

    DownloadManager::Instance().Init();
    LOGF("all subsystems initialized\n");

    bool running = true;
    while (aptMainLoop() && running) {
        int event = ui.HandleInput();

        switch (g_appState) {
            case AppState::SEARCH: {
                if (event == 1 || event == 4) {
                    std::string input = OpenKeyboard(g_searchQuery);
                    if (!input.empty()) {
                        g_searchQuery = input;
                        if (DoSearch(g_searchQuery)) {
                            g_appState = AppState::RESULTS;
                            ui.SetResults(g_searchResults);
                        }
                    }
                }
                break;
            }

            case AppState::RESULTS: {
                if (event == 2) {
                    g_appState = AppState::SEARCH;
                } else if (event == 1) {
                    int idx = ui.GetSelectedIndex();
                    if (idx >= 0 && idx < (int)g_searchResults.size()) {
                        DownloadVideo(g_searchResults[idx]);
                    }
                }
                break;
            }

            case AppState::DOWNLOADING: {
                auto& task = DownloadManager::Instance().GetCurrentTask();
                if (task.isComplete) {
                    g_appState = AppState::DOWNLOADED;
                } else if (task.isFailed) {
                    g_appState = AppState::RESULTS;
                    g_statusMessage = "Download failed: " + task.errorMsg;
                }
                break;
            }

            case AppState::DOWNLOADED: {
                if (event == 1 || event == 2) {
                    g_downloadedFiles = DownloadManager::Instance().GetDownloadedFiles();
                    g_appState = AppState::FILE_LIST;
                }
                break;
            }

            case AppState::FILE_LIST: {
                if (event == 2) {
                    g_appState = AppState::SEARCH;
                }
                break;
            }
        }

        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            running = false;
        }

        auto& task = DownloadManager::Instance().GetCurrentTask();
        ui.Render(g_appState, g_searchQuery, g_searchResults, task, g_downloadedFiles);
    }

    LOGF("=== 3dbili shutting down ===\n");
    DownloadManager::Instance().CancelDownload();
    ui.Exit();
    Http_Exit();
    hidExit();
    gfxExit();
    romfsExit();
    sdmcExit();
    fsExit();
    LOG_CLOSE();

    return 0;
}