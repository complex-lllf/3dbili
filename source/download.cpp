// 让 newlib 暴露 POSIX 目录接口
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

DownloadManager& DownloadManager::Instance() {
    static DownloadManager instance;
    return instance;
}

DownloadManager::DownloadManager() {
    LightLock_Init(&m_taskLock);
}

DownloadManager::~DownloadManager() {
    CancelDownload();
}

bool DownloadManager::Init() {
    struct stat st;

    if (stat(SD_DIR, &st) != 0) {
        mkdir(SD_DIR, 0777);
        LOGF("created directory %s\n", SD_DIR);
    }
    if (stat(DOWN_DIR, &st) != 0) {
        mkdir(DOWN_DIR, 0777);
        LOGF("created directory %s\n", DOWN_DIR);
    }
    return true;
}

std::string SanitizeFilename(const std::string& name) {
    std::string result = name;
    const char* illegal = "/\\:*?\"<>|";
    for (char& c : result) {
        if (strchr(illegal, c)) c = '_';
    }
    if (result.size() > 80) {
        result = result.substr(0, 80);
    }
    if (result.empty()) result = "untitled";
    return result;
}

bool DownloadManager::StartDownload(const std::string& bvid, const std::string& title,
                                    const std::string& directUrl) {
    if (m_isDownloading) return false;
    if (directUrl.empty()) return false;

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
    LightLock_Unlock(&m_taskLock);

    LOGF("start download: bvid=%s -> %s\n", bvid.c_str(), m_currentTask.outputPath.c_str());
    LOGF("  direct url: %s\n", directUrl.c_str());

    m_shouldCancel = false;
    m_isDownloading = true;

    s32 prio = 0x30;
    m_thread = threadCreate(DownloadThreadFunc, this, 8192 * 2, prio, -1, false);
    if (!m_thread) {
        LOGF("failed to create download thread\n");
        m_isDownloading = false;
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

void DownloadManager::CancelDownload() {
    if (m_isDownloading) {
        m_shouldCancel = true;
        if (m_thread) {
            threadJoin(m_thread, 2000000000);
            threadFree(m_thread);
            m_thread = nullptr;
        }
    }
    m_isDownloading = false;
}

void DownloadManager::DownloadThreadFunc(void* arg) {
    DownloadManager* self = (DownloadManager*)arg;

    HttpProgressCallback progressCb = [self](size_t current, size_t total) {
        LightLock_Lock(&self->m_taskLock);
        self->m_currentTask.downloadedBytes = current;
        self->m_currentTask.totalBytes = total;
        LightLock_Unlock(&self->m_taskLock);

        if (self->m_progressCallback) {
            self->m_progressCallback(current, total);
        }
    };

    // 拷贝需要的字段，避免后续访问 m_currentTask 时与其他线程竞争
    LightLock_Lock(&self->m_taskLock);
    std::string url = self->m_currentTask.directUrl;
    std::string path = self->m_currentTask.outputPath;
    LightLock_Unlock(&self->m_taskLock);

    HttpResponse resp = Http_DownloadToFile(url, path, progressCb);

    LightLock_Lock(&self->m_taskLock);
    if (self->m_shouldCancel) {
        LightLock_Unlock(&self->m_taskLock);
        remove(path.c_str());
        LightLock_Lock(&self->m_taskLock);
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Cancelled";
        LOGF("download cancelled\n");
    } else if (resp.success) {
        self->m_currentTask.isComplete = true;
        LOGF("download complete: %lu bytes\n", (unsigned long)resp.binarySize);
    } else {
        self->m_currentTask.isFailed = true;
        self->m_currentTask.errorMsg = "Download failed";
        LightLock_Unlock(&self->m_taskLock);
        remove(path.c_str());
        LightLock_Lock(&self->m_taskLock);
        LOGF("download failed\n");
    }
    LightLock_Unlock(&self->m_taskLock);

    self->m_isDownloading = false;
}

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

        if (name.size() > 4 && name.substr(name.size() - 4) == ".mp4") {
            files.push_back(std::string(DOWN_DIR) + "/" + name);
        }
    }
    closedir(dir);

    LOGF("found %d downloaded files\n", (int)files.size());
    return files;
}
