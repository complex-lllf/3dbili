#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>
#include "json_helper.h"
#include "download.h"

// UI 模块
// 职责：citro2d 渲染、屏幕键盘输入、触摸交互处理

enum class AppState {
    SEARCH,       // 搜索界面
    RESULTS,      // 搜索结果列表
    DOWNLOADING,  // 下载中
    DOWNLOADED,   // 下载完成
    FILE_LIST     // 已下载文件列表
};

class UIManager {
public:
    UIManager();
    ~UIManager();

    bool Init();
    void Exit();

    // 主渲染循环
    void Render(AppState state, const std::string& searchQuery,
                const std::vector<VideoSearchItem>& results,
                const DownloadTask& downloadTask,
                const std::vector<std::string>& downloadedFiles);

    // 处理输入，返回事件
    // 返回值：0=无操作，1=确认，2=取消/返回，3=打开键盘
    int HandleInput();

    // 获取选中的索引
    int GetSelectedIndex() const { return m_selectedIndex; }

    // 更新结果列表数据
    void SetResults(const std::vector<VideoSearchItem>& results);

    // 封面纹理管理
    C2D_Image LoadCoverTexture(const std::string& url);
    void ClearTextures();

private:
    // citro2d 相关
    C3D_RenderTarget* m_topTarget;
    C3D_RenderTarget* m_bottomTarget;
    C2D_TextBuf m_textBuf;
    C2D_Font m_font;

    // 界面元素
    float m_screenWidth;
    float m_screenHeight;

    // 状态
    int m_selectedIndex;
    int m_scrollOffset;

    // 封面纹理缓存
    struct CoverEntry {
        std::string url;
        C2D_Image image;
        C3D_Tex tex;
        bool valid;
    };
    std::vector<CoverEntry> m_coverCache;

    // 辅助绘制函数
    void DrawSearchScreen(const std::string& query, const std::string& statusMsg);
    void DrawResultsList(const std::vector<VideoSearchItem>& results);
    void DrawDownloadScreen(const DownloadTask& task);
    void DrawFileList(const std::vector<std::string>& files);

    // 文本绘制辅助
    void DrawText(const std::string& text, float x, float y, float scale, u32 color);
    void DrawRect(float x, float y, float w, float h, u32 color);

    // 封面加载
    C2D_Image* GetCoverTexture(const std::string& url);
};