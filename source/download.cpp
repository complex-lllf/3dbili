#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "download.h"
#include "http.h"
#include "log.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <3ds.h>

// 线程栈：32KB。8192*2=16KB 太小，下载线程调用链中包含
// std::function / std::string 拷贝与 HTTP 缓冲区，容易栈溢出。
static constexpr int DOWNLOAD_THREAD_STACK = 32 * 1024;
static constexpr s32 DOWNLOAD_THREAD_PRIO  = 0x30;
static constexpr u64 DOWNLOAD_JOIN_TIMEOUT_NS = 2000000000ULL; // 2s

DownloadManager& DownloadManager::Instance() {
    static DownloadManager instance;
    return instance;
}

DownloadManager::DownloadManager() {
    LightLock_Init(&m_taskLock);
}

DownloadManager::~DownloadManager() {
    CancelDownload();
    CleanupThreadIfDone();
}

// ---------------------------------------------------------------------------
// 目录创建：标准 mkdir 优先，失败时回退到 FSUSER_CreateDirectory
// ---------------------------------------------------------------------------
bool DownloadManager::EnsureDir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return true;

    // 先尝试标准 mkdir（devkitARM 下 newlib 通常能正确处理 sdmc: 前缀）
    if (mkdir(path, 0777) == 0) return true;

    // 回退：打开 SDMC 根归档，用相对路径创建
    FS_Archive sdArch;
    Result rc = FSUSER_OpenArchive(&sdArch, ARCHIVE_SDMC,
                                   fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(rc)) {
        LOGF("FSUSER_OpenArchive failed: 0x%08lX\n", (unsigned long)rc);
        return false;
    }

    // 去掉可能的 "sdmc:" 前缀，得到相对归档根的路径
    const char* rel = path;
    if (strncmp(path, "sdmc:", 5) == 0) rel = path + 5;
    if (rel[0] == '\0') rel = "/";

    rc = FSUSER_CreateDirectory(sdArch, fsMakePath(PATH_ASCII, rel), 0);
    FSUSER_CloseArchive(sdArch);

    // 0xC82044BE = FSUSER_DIRECTORY_ALREADY_EXISTS
    if (R_SUCCEEDED(rc) || (u32)rc == 0xC82044BE) {
        return true;
    }

    LOGF("FSUSER_CreateDirectory(%s) failed: 0x%08lX\n",
         rel, (unsigned long)rc);
    return false;
}

bool DownloadManager::Init() {
    if (!EnsureDir(SD_DIR)) {
        LOGF("failed to create %s\n", SD_DIR);
        return false;
    }
    if (!EnsureDir(DOWN_DIR)) {
        LOGF("failed to create %s\n", DOWN_DIR);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 文件名净化
// ---------------------------------------------------------------------------
std::string SanitizeFilename(const std::string& name) {
    std::string result = name;
    const char* illegal = "/\\:*?\"<>|";
    for (char& c : result) {
        if (strchr(illegal, c)) c = '_';
    }

    const size_t MAX_BYTES = 80;
    if (result.size() > MAX_BYTES) {
        size_t cut = MAX_BYTES;
        // 向前回退到字符首字节；cut 最多降到 0
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) {
            cut--;
        }
        // 若回退到 0（理论上不会发生），退回一个字符
        if (cut == 0) cut = MAX_BYTES;
        result = result.substr(0, cut);
    }

    if (result.empty()) result = "untitled";
    return result;
}

// ---------------------------------------------------------------------------
// 线程句柄回收
// ---------------------------------------------------------------------------
void DownloadManager::CleanupThreadIfDone() {
    if (m_thread == nullptr) return;
    if (m_isDownloading.load()) return;  // 还在跑，别动

    // timeout=0：只回收已结束的线程，绝不阻塞
    threadJoin(m_thread, 0);
    threadFree(m_thread);
    m_thread = nullptr;
}

// ---------------------------------------------------------------------------
// 启动下载
// ---------------------------------------------------------------------------
bool DownloadManager::StartDownload(const std::string& bvid, const std::string& title,
                                    const std::string& directUrl) {
    if (m_isDownloading.load()) return false;
    if (directUrl.empty()) return false;

    // 启动前顺手回收上一次遗留的线程句柄，避免堆叠
    CleanupThreadIfDone();

    std::string outputPath;

    LightLock_Lock(&m_taskLock);
    m_currentTask.bvid = bvid;
    m_currentTask.title = title;
    m_currentTask.directUrl = directUrl;
    m_currentTask.downloadedBytes = 0;
    m_currentTask.totalBytes = 0;
    m_currentTask.isComplete = false;
    m_currentTask.isFailed = false;
    m_currentTask.errorMsg.clear();

    std::string safeName = SanitizeFilename(title);
    m_currentTask.outputPath = std::string(DOWN_DIR) + "/" + safeName + ".mp4";
    outputPath = m_currentTask.outputPath;
    LightLock_Unlock(&m_taskLock);

    LOGF("start download: bvid=%s -> %s\n", bvid.c_str(), outputPath.c_str());
    LOGF("  direct url: %s\n", directUrl.c_str());

    m_shouldCancel.store(false);
    m_isDownloading.store(true);

    m_thread = threadCreate(DownloadThreadFunc, this,
                            DOWNLOAD_THREAD_STACK, DOWNLOAD_THREAD_PRIO,
                            -1, false);
    if (!m_thread) {
        LOGF("failed to create download thread\n");
        m_isDownloading.store(false);
        LightLock_Lock(&m_taskLock);
        m_currentTask.isFailed = true;
        m_currentTask.errorMsg = "Failed to create download thread";
        LightLock_Unlock(&m_taskLock);
        return false;
    }

    return true;
}

DownloadTask DownloadManager::GetCurrentTask() const {
    LightLock_Lock(&m_taskLock);
    DownloadTask copy = m_currentTask;
    LightLock_Unlock(&m_taskLock);
    return copy;
}

// ---------------------------------------------------------------------------
// 取消下载
// ---------------------------------------------------------------------------
void DownloadManager::CancelDownload() {
    m_shouldCancel.store(true);

    if (m_thread) {
        // 【关键修复】若线程仍在跑，给它最多 2 秒优雅退出。
        // 如果超时，绝对不能立即 threadFree，否则线程仍在使用 this 指针，
        // 会导致 Use-After-Free，表现为向 0x00000008 写入数据导致 Data Abort。
        Result rc = threadJoin(m_thread, DOWNLOAD_JOIN_TIMEOUT_NS);
        if (R_SUCCEEDED(rc)) {
            threadFree(m_thread);
            m_thread = nullptr;
        } else {
            LOGF("cancel timeout, thread still running. Will cleanup later.\n");
            // 不释放，留给 CleanupThreadIfDone 处理
        }
    }

    m_isDownloading.store(false);
}

// ---------------------------------------------------------------------------
// 下载线程主体
// ---------------------------------------------------------------------------
void DownloadManager::DownloadThreadFunc(void* arg) {
    DownloadManager* self = static_cast<DownloadManager*>(arg);

    // 进度回调：单次加锁完成「写进度」+「取回调副本」
    HttpProgressCallback progressCb = [self](size_t current, size_t total) {
        std::function<void(size_t, size_t)> cb;
        LightLock_Lock(&self->m_taskLock);
        self->m_currentTask.downloadedBytes = current;
        self->m_currentTask.totalBytes = total;
        cb = self->m_progressCallback;
        LightLock_Unlock(&self->m_taskLock);
        if (cb) cb(current, total);
    };

    // 拷贝任务字段，避免下载中与其他线程读写 m_currentTask 竞争
    std::string url, path;
    LightLock_Lock(&self->m_taskLock);
    url  = self->m_currentTask.directUrl;
    path = self->m_currentTask.outputPath;
    LightLock_Unlock(&self->m_taskLock);

    HttpResponse resp = Http_DownloadToFile(url, path, progressCb);

    LightLock_Lock(&self->m_taskLock);
    if (self->m_shouldCancel.load()) {
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Cancelled";
        LightLock_Unlock(&self->m_taskLock);
        remove(path.c_str());
        LOGF("download cancelled\n");
    } else if (resp.success) {
        self->m_currentTask.isComplete = true;
        LightLock_Unlock(&self->m_taskLock);
        LOGF("download complete: %lu bytes\n", (unsigned long)resp.binarySize);
    } else {
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Download failed";
        LightLock_Unlock(&self->m_taskLock);
        remove(path.c_str());
        LOGF("download failed\n");
    }

    // 只置位，不在这里 free 自己的句柄（线程不能 free 自己）。
    // 由主线程调用 CleanupThreadIfDone() 或下一次 StartDownload 回收。
    self->m_isDownloading.store(false);
}

// ---------------------------------------------------------------------------
// 枚举已下载文件
// ---------------------------------------------------------------------------
std::vector<std::string> DownloadManager::GetDownloadedFiles() const {
    std::vector<std::string> files;

    DIR* dir = opendir(DOWN_DIR);
    if (!dir) {
        LOGF("cannot open %s\n", DOWN_DIR);
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        // 【修复】d_type 在 FAT/exFAT 上可能不可靠，改用 stat 检测子目录
        std::string fullPath = std::string(DOWN_DIR) + "/" + name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            continue;
        }

        if (name.size() > 4 && name.substr(name.size() - 4) == ".mp4") {
            files.push_back(fullPath);
        }
    }
    closedir(dir);

    LOGF("found %d downloaded files\n", (int)files.size());
    return files;
}