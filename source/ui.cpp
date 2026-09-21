#include "ui.h"
#include "http.h"
#include <cstring>
#include <cstdio>

UIManager::UIManager()
    : m_topTarget(nullptr), m_bottomTarget(nullptr), m_font(nullptr),
      m_screenWidth(400.0f), m_screenHeight(240.0f),
      m_selectedIndex(0), m_scrollOffset(0) {
}

UIManager::~UIManager() {
    Exit();
}

bool UIManager::Init() {
    // 初始化 citro2d
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) return false;
    C2D_Prepare();

    // 创建渲染目标（上屏：400x240，下屏：320x240）
    m_topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    m_bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // 创建文本缓冲
    m_textBuf = C2D_TextBufNew(8192);

    return true;
}

void UIManager::Exit() {
    ClearTextures();
    if (m_textBuf) {
        C2D_TextBufDelete(m_textBuf);
        m_textBuf = nullptr;
    }
    if (m_font) {
        C2D_FontFree(m_font);
        m_font = nullptr;
    }
    C2D_Fini();
}

void UIManager::ClearTextures() {
    for (auto& entry : m_coverCache) {
        if (entry.valid) {
            C3D_TexDelete(&entry.tex);
        }
    }
    m_coverCache.clear();
}

C2D_Image* UIManager::GetCoverTexture(const std::string& url) {
    if (url.empty()) return nullptr;

    // 查找缓存
    for (auto& entry : m_coverCache) {
        if (entry.url == url && entry.valid) {
            return &entry.image;
        }
    }

    // 缓存未命中，下载图片
    HttpResponse resp = Http_DownloadToMemory(url);
    if (!resp.success || !resp.binaryData || resp.binarySize < 100) {
        free(resp.binaryData);
        return nullptr;
    }

    // B站封面通常是 JPEG，citro2d 不直接支持 JPEG 解码。
    // 此处简化处理：创建占位纹理。
    // 完整实现需集成 jpeg 解码库（如 libjpeg-turbo）将 JPEG 转为 RGBA。
    // 本示例中返回 nullptr，UI 显示占位矩形。

    free(resp.binaryData);
    return nullptr;
}

void UIManager::DrawText(const std::string& text, float x, float y, float scale, u32 color) {
    C2D_Text c2dText;
    C2D_TextParse(&c2dText, m_textBuf, text.c_str());
    C2D_TextOptimize(&c2dText);
    C2D_DrawText(&c2dText, C2D_WithColor, x, y, 0.5f, scale, color);
}

void UIManager::DrawRect(float x, float y, float w, float h, u32 color) {
    C2D_DrawRectSolid(x, y, 0.0f, w, h, color);
}

void UIManager::Render(AppState state, const std::string& searchQuery,
                       const std::vector<VideoSearchItem>& results,
                       const DownloadTask& downloadTask,
                       const std::vector<std::string>& downloadedFiles) {
    // 清空文本缓冲
    C2D_TextBufClear(m_textBuf);

    // ========== 渲染上屏 ==========
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(m_topTarget, C2D_Color32(30, 30, 40, 255));
    C2D_SceneBegin(m_topTarget);

    switch (state) {
        case AppState::SEARCH:
            DrawSearchScreen(searchQuery, "Press A to open keyboard");
            break;
        case AppState::RESULTS:
            DrawResultsList(results);
            break;
        case AppState::DOWNLOADING:
            DrawDownloadScreen(downloadTask);
            break;
        case AppState::DOWNLOADED:
            DrawDownloadScreen(downloadTask);
            DrawText("Download complete!", 10, 200, 0.5f, C2D_Color32(0, 255, 0, 255));
            break;
        case AppState::FILE_LIST:
            DrawFileList(downloadedFiles);
            break;
    }

    // ========== 渲染下屏 ==========
    C2D_TargetClear(m_bottomTarget, C2D_Color32(20, 20, 30, 255));
    C2D_SceneBegin(m_bottomTarget);

    // 下屏状态栏
    DrawRect(0, 0, 320, 20, C2D_Color32(40, 40, 60, 255));
    DrawText("Bilibili 3DS", 5, 2, 0.4f, C2D_Color32(255, 200, 100, 255));

    if (state == AppState::RESULTS) {
        DrawText("Up/Down: Select  A: Download  B: Back", 5, 220, 0.35f,
                 C2D_Color32(150, 150, 150, 255));
    } else if (state == AppState::FILE_LIST) {
        DrawText("Downloaded files. B: Back", 5, 220, 0.35f,
                 C2D_Color32(150, 150, 150, 255));
    }

    C3D_FrameEnd(0);
}

void UIManager::DrawSearchScreen(const std::string& query, const std::string& statusMsg) {
    // 标题栏
    DrawRect(0, 0, 400, 40, C2D_Color32(50, 80, 120, 255));
    DrawText("Bilibili Search", 10, 8, 0.6f, C2D_Color32(255, 255, 255, 255));

    // 搜索框
    DrawRect(20, 60, 360, 40, C2D_Color32(60, 60, 80, 255));
    DrawText(query.empty() ? "(tap to type)" : query, 30, 70, 0.5f,
             query.empty() ? C2D_Color32(120, 120, 120, 255) : C2D_Color32(255, 255, 255, 255));

    // 状态提示
    DrawText(statusMsg, 20, 120, 0.45f, C2D_Color32(180, 180, 180, 255));
}

void UIManager::DrawResultsList(const std::vector<VideoSearchItem>& results) {
    DrawRect(0, 0, 400, 30, C2D_Color32(50, 80, 120, 255));
    DrawText("Search Results", 10, 5, 0.5f, C2D_Color32(255, 255, 255, 255));

    float y = 40.0f;
    const float itemHeight = 50.0f;
    int visibleCount = (int)((m_screenHeight - 40) / itemHeight);

    int startIdx = m_scrollOffset;
    int endIdx = startIdx + visibleCount;
    if (endIdx > (int)results.size()) endIdx = results.size();

    for (int i = startIdx; i < endIdx; i++) {
        const auto& item = results[i];
        float itemY = y + (i - startIdx) * itemHeight;

        // 选中高亮
        u32 bgColor = (i == m_selectedIndex)
            ? C2D_Color32(60, 90, 140, 255)
            : C2D_Color32(40, 40, 55, 255);
        DrawRect(5, itemY, 390, itemHeight - 4, bgColor);

        // 封面占位
        DrawRect(10, itemY + 5, 60, 40, C2D_Color32(80, 80, 100, 255));

        // 标题（截断过长标题）
        std::string title = item.title;
        if (title.size() > 30) title = title.substr(0, 27) + "...";
        DrawText(title, 78, itemY + 5, 0.45f, C2D_Color32(255, 255, 255, 255));

        // 作者与播放量
        DrawText(item.author + "  " + std::to_string(item.playCount), 78, itemY + 25, 0.35f,
                 C2D_Color32(160, 160, 160, 255));
    }
}

void UIManager::DrawDownloadScreen(const DownloadTask& task) {
    DrawRect(0, 0, 400, 30, C2D_Color32(50, 80, 120, 255));
    DrawText("Downloading...", 10, 5, 0.5f, C2D_Color32(255, 255, 255, 255));

    // 标题
    std::string title = task.title;
    if (title.size() > 40) title = title.substr(0, 37) + "...";
    DrawText(title, 20, 50, 0.5f, C2D_Color32(255, 255, 255, 255));

    // 进度条背景
    DrawRect(20, 100, 360, 20, C2D_Color32(60, 60, 80, 255));

    // 进度条填充
    float progress = 0.0f;
    if (task.totalBytes > 0) {
        progress = (float)task.downloadedBytes / (float)task.totalBytes;
    } else if (task.downloadedBytes > 0) {
        progress = -1.0f; // 未知总大小
    }

    if (progress >= 0.0f) {
        DrawRect(20, 100, 360 * progress, 20, C2D_Color32(100, 180, 100, 255));
    } else {
        // 未知进度：显示动画条
        DrawRect(20, 100, 360, 20, C2D_Color32(100, 180, 100, 128));
    }

    // 字节数
    char sizeBuf[128];
    if (task.totalBytes > 0) {
        snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB / %.1f MB",
                 task.downloadedBytes / 1048576.0, task.totalBytes / 1048576.0);
    } else {
        snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB downloaded",
                 task.downloadedBytes / 1048576.0);
    }
    DrawText(sizeBuf, 20, 130, 0.5f, C2D_Color32(200, 200, 200, 255));

    if (task.isFailed) {
        DrawText("Error: " + task.errorMsg, 20, 160, 0.5f, C2D_Color32(255, 100, 100, 255));
    }
}

void UIManager::DrawFileList(const std::vector<std::string>& files) {
    DrawRect(0, 0, 400, 30, C2D_Color32(50, 80, 120, 255));
    DrawText("Downloaded Videos", 10, 5, 0.5f, C2D_Color32(255, 255, 255, 255));

    float y = 40.0f;
    const float itemHeight = 25.0f;

    if (files.empty()) {
        DrawText("No downloaded files found.", 20, 50, 0.5f, C2D_Color32(150, 150, 150, 255));
        return;
    }

    for (size_t i = 0; i < files.size() && i < 7; i++) {
        // 提取文件名
        std::string path = files[i];
        size_t lastSlash = path.find_last_of('/');
        std::string name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

        if (name.size() > 40) name = name.substr(0, 37) + "...";

        DrawRect(5, y + i * itemHeight, 390, itemHeight - 3, C2D_Color32(40, 40, 55, 255));
        DrawText(name, 15, y + i * itemHeight + 3, 0.4f, C2D_Color32(220, 220, 220, 255));
    }
}

int UIManager::HandleInput() {
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    touchPosition touch;
    hidTouchRead(&touch);
    u32 touchKeys = hidKeysDown();

    // 触摸事件
    if (touchKeys & KEY_TOUCH) {
        // 下屏触摸区域判断
        if (touch.py >= 0 && touch.py <= 240) {
            // 简单处理：触摸返回特殊值
            return 4; // touch event
        }
    }

    // 按键处理
    if (kDown & KEY_A) return 1;       // 确认
    if (kDown & KEY_B) return 2;       // 取消/返回
    if (kDown & KEY_Y) return 3;       // 打开键盘
    if (kDown & KEY_DOWN) {
        m_selectedIndex++;
        return 0;
    }
    if (kDown & KEY_UP) {
        if (m_selectedIndex > 0) m_selectedIndex--;
        return 0;
    }
    if (kHeld & KEY_DOWN) return 5;    // 持续下滑
    if (kHeld & KEY_UP) return 6;      // 持续上滑

    return 0;
}

void UIManager::SetResults(const std::vector<VideoSearchItem>& results) {
    m_selectedIndex = 0;
    m_scrollOffset = 0;
}