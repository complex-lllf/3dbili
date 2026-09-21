#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>
#include <cstdio>

#include "http.h"
#include "json_helper.h"
#include "ui.h"
#include "download.h"

// ==================== B站 API 常量 ====================
// 注意：3DS 原生 httpc 不支持 TLS 1.2，B站 HTTPS 接口无法直接访问。
// 以下使用 HTTP 端点，或可通过自建代理转发。

// 搜索接口（搜索关键词，返回 JSON）
static const char* SEARCH_API = "http://api.bilibili.com/x/web-interface/search/type"
                                "?search_type=video&keyword=%s&page=1";

// 视频详情接口（通过 bvid 获取 aid 和 cid）
static const char* VIEW_API = "http://api.bilibili.com/x/web-interface/view?bvid=%s";

// 播放地址接口（fnval=1 返回 MP4 直链，qn=16 为 360P）
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

// ==================== 搜索功能 ====================
bool DoSearch(const std::string& keyword) {
    if (keyword.empty()) return false;

    g_statusMessage = "Searching...";

    char url[1024];
    snprintf(url, sizeof(url), SEARCH_API, UrlEncode(keyword).c_str());

    HttpResponse resp = Http_Get(url);
    if (!resp.success || resp.body.empty()) {
        g_statusMessage = "Search failed. Check network.";
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

// ==================== 获取视频信息并下载 ====================
bool DownloadVideo(const VideoSearchItem& item) {
    auto& dm = DownloadManager::Instance();
    if (dm.IsDownloading()) {
        g_statusMessage = "Already downloading.";
        return false;
    }

    // Step 1: 调用 view 接口获取 aid 和 cid
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

    // Step 2: 调用 playurl 接口获取 MP4 直链
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

    // Step 3: 启动下载
    if (!dm.StartDownload(item.bvid, item.title, directUrl)) {
        g_statusMessage = "Failed to start download.";
        return false;
    }

    g_appState = AppState::DOWNLOADING;
    return true;
}

// ==================== 屏幕键盘输入 ====================
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

// ==================== 主函数 ====================
int main(int argc, char** argv) {
    // 初始化服务
    romfsInit();
    gfxInitDefault();
    hidInit();

    if (!Http_Init()) {
        // HTTP 初始化失败，仍然显示错误并退出
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
        return 1;
    }

    // 设置 B站 请求头
    Http_SetHeaders(
        "Mozilla/5.0 (Nintendo 3DS; Linux) AppleWebKit/605.1.15",
        "https://www.bilibili.com"
    );

    // 初始化 UI
    UIManager ui;
    if (!ui.Init()) {
        Http_Exit();
        hidExit();
        gfxExit();
        romfsExit();
        return 1;
    }

    // 初始化下载管理器
    DownloadManager::Instance().Init();

    // 主循环
    bool running = true;
    while (aptMainLoop() && running) {
        // 处理输入
        int event = ui.HandleInput();

        switch (g_appState) {
            case AppState::SEARCH: {
                if (event == 1 || event == 4) {
                    // 打开屏幕键盘
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
                    // 返回搜索
                    g_appState = AppState::SEARCH;
                } else if (event == 1) {
                    // 下载选中的视频
                    int idx = ui.GetSelectedIndex();
                    if (idx >= 0 && idx < (int)g_searchResults.size()) {
                        DownloadVideo(g_searchResults[idx]);
                    }
                } else if (event == 4) {
                    // 触摸选择
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

        // 检查退出
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            running = false;
        }

        // 渲染
        auto& task = DownloadManager::Instance().GetCurrentTask();
        ui.Render(g_appState, g_searchQuery, g_searchResults, task, g_downloadedFiles);
    }

    // 清理
    DownloadManager::Instance().CancelDownload();
    ui.Exit();
    Http_Exit();
    hidExit();
    gfxExit();
    romfsExit();

    return 0;
}