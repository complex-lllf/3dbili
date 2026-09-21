#pragma once

#include <string>
#include <functional>
#include <vector>
#include <3ds.h>

// 下载管理模块
// 职责：管理视频下载任务、进度追踪、SD 卡文件读写

struct DownloadTask {
    std::string bvid;
    std::string title;
    std::string outputPath;    // SD 卡路径
    std::string directUrl;     // MP4 直链
    size_t totalBytes;
    size_t downloadedBytes;
    bool isComplete;
    bool isFailed;
    std::string errorMsg;
};

// 下载管理器
class DownloadManager {
public:
    // 获取单例
    static DownloadManager& Instance();

    // 初始化（确保 SD 卡目录存在）
    bool Init();

    // 启动下载任务（非阻塞，在后台线程执行）
    bool StartDownload(const std::string& bvid, const std::string& title,
                       const std::string& directUrl);

    // 获取当前任务状态
    const DownloadTask& GetCurrentTask() const { return m_currentTask; }
    bool IsDownloading() const { return m_isDownloading; }

    // 取消当前下载
    void CancelDownload();

    // 获取已下载文件列表（扫描 /3ds/bilibili/ 目录）
    std::vector<std::string> GetDownloadedFiles() const;

    // 进度回调（从后台线程调用，需线程安全地更新 UI 状态）
    void SetProgressCallback(std::function<void(size_t, size_t)> cb) {
        m_progressCallback = cb;
    }

private:
    DownloadManager() = default;
    ~DownloadManager();

    // 后台下载线程
    static void DownloadThreadFunc(void* arg);

    DownloadTask m_currentTask;
    volatile bool m_isDownloading = false;
    volatile bool m_shouldCancel = false;

    // 线程句柄
    Thread m_thread = nullptr;

    std::function<void(size_t, size_t)> m_progressCallback;

    // 目录路径
    static constexpr const char* SD_DIR = "/3ds/bilibili";
};

// 清理文件名中的非法字符
std::string SanitizeFilename(const std::string& name);