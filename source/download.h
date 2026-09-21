#pragma once

#include <string>
#include <functional>
#include <vector>
#include <3ds.h>

// 下载管理模块
// 职责：管理视频下载任务、进度追踪、SD 卡文件读写
//
// 路径约定：
//   SD_DIR   = sdmc:/3dbili            （应用根目录）
//   DOWN_DIR = sdmc:/3dbili/download   （视频下载目录）
// 使用 sdmc: 前缀是为了兼容 CIA 运行环境（CIA 不会自动挂载 sdmc:）

struct DownloadTask {
    std::string bvid;          // 视频 BV 号
    std::string title;         // 视频标题
    std::string outputPath;    // SD 卡路径（.mp4 完整路径）
    std::string directUrl;     // MP4 直链
    size_t totalBytes;         // 总字节数（0 表示未知）
    size_t downloadedBytes;    // 已下载字节数
    bool isComplete;           // 是否完成
    bool isFailed;             // 是否失败
    std::string errorMsg;      // 失败原因
};

// 下载管理器（单例）
class DownloadManager {
public:
    // 获取单例
    static DownloadManager& Instance();

    // 初始化（确保 SD 卡目录存在）
    bool Init();

    // 启动下载任务（非阻塞，在后台线程执行）
    bool StartDownload(const std::string& bvid, const std::string& title,
                       const std::string& directUrl);

    // 获取当前任务状态（返回拷贝，内部加锁，防止与下载线程数据竞争）
    DownloadTask GetCurrentTask() const;

    bool IsDownloading() const { return m_isDownloading; }

    // 取消当前下载
    void CancelDownload();

    // 获取已下载文件列表（扫描 sdmc:/3dbili/download/）
    std::vector<std::string> GetDownloadedFiles() const;

    // 进度回调（从后台线程调用，需线程安全地更新 UI 状态）
    void SetProgressCallback(std::function<void(size_t, size_t)> cb) {
        m_progressCallback = cb;
    }

private:
    DownloadManager();
    ~DownloadManager();

    // 禁止拷贝
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    // 后台下载线程入口
    static void DownloadThreadFunc(void* arg);

    DownloadTask m_currentTask;
    volatile bool m_isDownloading = false;
    volatile bool m_shouldCancel = false;

    // 线程句柄
    Thread m_thread = nullptr;

    std::function<void(size_t, size_t)> m_progressCallback;

    // 保护 m_currentTask 的轻量互斥锁（libctru LightLock）
    mutable LightLock m_taskLock;

    // 目录路径（sdmc: 前缀，CIA / 3dsx 通用）
    static constexpr const char* SD_DIR   = "sdmc:/3dbili";
    static constexpr const char* DOWN_DIR = "sdmc:/3dbili/download";
};

// 清理文件名中的非法字符
std::string SanitizeFilename(const std::string& name);
