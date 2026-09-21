#include "download.h"
#include "http.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

DownloadManager& DownloadManager::Instance() {
    static DownloadManager instance;
    return instance;
}

DownloadManager::~DownloadManager() {
    CancelDownload();
}

bool DownloadManager::Init() {
    // 确保目录存在
    struct stat st;
    if (stat(SD_DIR, &st) != 0) {
        mkdir(SD_DIR, 0777);
    }
    return true;
}

std::string SanitizeFilename(const std::string& name) {
    std::string result = name;
    // 替换文件系统非法字符
    const char* illegal = "/\\:*?\"<>|";
    for (char& c : result) {
        if (strchr(illegal, c)) c = '_';
    }
    // 限制长度
    if (result.size() > 80) {
        result = result.substr(0, 80);
    }
    // 避免空名
    if (result.empty()) result = "untitled";
    return result;
}

bool DownloadManager::StartDownload(const std::string& bvid, const std::string& title,
                                    const std::string& directUrl) {
    if (m_isDownloading) return false;
    if (directUrl.empty()) return false;

    // 设置当前任务
    m_currentTask.bvid = bvid;
    m_currentTask.title = title;
    m_currentTask.directUrl = directUrl;
    m_currentTask.downloadedBytes = 0;
    m_currentTask.totalBytes = 0;
    m_currentTask.isComplete = false;
    m_currentTask.isFailed = false;
    m_currentTask.errorMsg.clear();

    // 构造输出路径
    std::string safeName = SanitizeFilename(title);
    m_currentTask.outputPath = std::string(SD_DIR) + "/" + safeName + ".mp4";

    m_shouldCancel = false;
    m_isDownloading = true;

    // 创建后台线程
    s32 prio = 0x30; // 中等优先级
    m_thread = threadCreate(DownloadThreadFunc, this, 8192 * 2, prio, -1, false);
    if (!m_thread) {
        m_isDownloading = false;
        m_currentTask.isFailed = true;
        m_currentTask.errorMsg = "Failed to create download thread";
        return false;
    }

    return true;
}

void DownloadManager::CancelDownload() {
    if (m_isDownloading) {
        m_shouldCancel = true;
        // 等待线程结束
        if (m_thread) {
            threadJoin(m_thread, 2000000000); // 2 秒超时
            threadFree(m_thread);
            m_thread = nullptr;
        }
    }
    m_isDownloading = false;
}

void DownloadManager::DownloadThreadFunc(void* arg) {
    DownloadManager* self = (DownloadManager*)arg;

    // 进度回调桥接
    auto progressCb = [self](size_t current, size_t total) {
        self->m_currentTask.downloadedBytes = current;
        self->m_currentTask.totalBytes = total;
        if (self->m_progressCallback) {
            self->m_progressCallback(current, total);
        }
        // 检查取消标志
        if (self->m_shouldCancel) {
            // 通过抛出异常或直接返回中断下载（此处简单处理：httpc 的读取循环会持续，
            // 实际项目中需要更精细的中断机制。这里我们接受下载完成后再检查取消）
        }
    };

    HttpResponse resp = Http_DownloadToFile(
        self->m_currentTask.directUrl,
        self->m_currentTask.outputPath,
        progressCb);

    if (self->m_shouldCancel) {
        // 用户取消，删除部分文件
        remove(self->m_currentTask.outputPath.c_str());
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Cancelled";
    } else if (resp.success) {
        self->m_currentTask.isComplete = true;
    } else {
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Download failed";
        remove(self->m_currentTask.outputPath.c_str());
    }

    self->m_isDownloading = false;
}

std::vector<std::string> DownloadManager::GetDownloadedFiles() const {
    std::vector<std::string> files;

    DIR* dir = opendir(SD_DIR);
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        // 只列出 .mp4 文件
        if (name.size() > 4 && name.substr(name.size() - 4) == ".mp4") {
            files.push_back(std::string(SD_DIR) + "/" + name);
        }
    }
    closedir(dir);

    return files;
}