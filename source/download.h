#pragma once

#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <3ds.h>

// 下载管理模块
// 职责：管理视频下载任务、进度追踪、SD 卡文件读写
//
// 路径约定：
//   SD_DIR   = sdmc:/3dbili            （应用根目录）
//   DOWN_DIR = sdmc:/3dbili/download   （视频下载目录）
// 使用 sdmc: 前缀是为了兼容 CIA 运行环境（CIA 不会自动挂载 sdmc:）
//
// 线程模型：
//   - 下载在独立线程中进行，线程栈 32KB（见 download.cpp）
//   - m_currentTask 通过 LightLock m_taskLock 保护
//   - m_isDownloading / m_shouldCancel 为 atomic，跨线程无锁读写
//   - 线程句柄 m_thread 的回收由主线程负责，见 CleanupThreadIfDone()

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
    static DownloadManager& Instance();

    // 初始化：确保 SD_DIR / DOWN_DIR 存在
    bool Init();

    // 启动一次下载。若已有下载在进行中返回 false。
    // 内部会先调用 CleanupThreadIfDone() 回收上一次遗留的线程句柄。
    bool StartDownload(const std::string& bvid, const std::string& title,
                       const std::string& directUrl);

    // 获取当前任务快照（线程安全）
    DownloadTask GetCurrentTask() const;

    bool IsDownloading() const { return m_isDownloading.load(); }

    // 取消下载：置位 m_shouldCancel，join 下载线程（最多 2 秒），释放句柄。
    // 若线程已自然结束但句柄未回收，本函数同样会完成回收。
    void CancelDownload();

    // 回收已自然结束的下载线程句柄。
    // 由于线程无法 free 自身，下载完成后 m_thread 会保留至：
    //   - 主线程调用本函数，或
    //   - 下一次 StartDownload 的前置清理
    // 建议在主循环每帧调用一次，避免句柄堆积。
    void CleanupThreadIfDone();

    // 枚举 sdmc:/3dbili/download 下的 .mp4 文件
    std::vector<std::string> GetDownloadedFiles() const;

    // 设置进度回调（size_t current, size_t total）
    // 回调在下载线程上下文中执行，内部实现应保持轻量。
    void SetProgressCallback(std::function<void(size_t, size_t)> cb) {
        LightLock_Lock(&m_taskLock);
        m_progressCallback = std::move(cb);
        LightLock_Unlock(&m_taskLock);
    }

private:
    DownloadManager();
    ~DownloadManager();

    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    static void DownloadThreadFunc(void* arg);

    // 确保目录存在，兼容 CIA / 3dsx。
    // 先尝试 mkdir，失败则回退到 FSUSER_CreateDirectory（自动剥离 sdmc: 前缀）。
    static bool EnsureDir(const char* path);

    DownloadTask m_currentTask;

    // 跨线程可见的原子标志
    std::atomic<bool> m_isDownloading{false};
    std::atomic<bool> m_shouldCancel{false};

    // 下载线程句柄。非 nullptr 表示「线程对象尚未回收」，
    // 不代表线程仍在运行（下载完成后线程会自然结束但句柄保留）。
    Thread m_thread = nullptr;

    std::function<void(size_t, size_t)> m_progressCallback;

    mutable LightLock m_taskLock;

    static constexpr const char* SD_DIR   = "sdmc:/3dbili";
    static constexpr const char* DOWN_DIR = "sdmc:/3dbili/download";
};

std::string SanitizeFilename(const std::string& name);