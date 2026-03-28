#include "file_watcher.h"
#include <iostream>
#include <sys/stat.h>

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    Stop();
}

static long long GetFileModTime(const std::string& path) {
    struct _stat64 st;
    if (_stat64(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

void FileWatcher::Watch(const std::string& filePath, Callback callback, int intervalMs) {
    Stop();

    callback_ = std::move(callback);
    intervalMs_ = intervalMs;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        WatchEntry entry;
        entry.filePath = filePath;
        entry.lastModTime = GetFileModTime(filePath);
        entries_.push_back(entry);
    }

    watching_ = true;
    thread_ = std::thread(&FileWatcher::WatchThread, this);
}

void FileWatcher::AddFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    WatchEntry entry;
    entry.filePath = filePath;
    entry.lastModTime = GetFileModTime(filePath);
    entries_.push_back(entry);
}

void FileWatcher::Stop() {
    watching_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void FileWatcher::WatchThread() {
    while (watching_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : entries_) {
            long long modTime = GetFileModTime(entry.filePath);
            if (modTime > entry.lastModTime && entry.lastModTime > 0) {
                entry.lastModTime = modTime;
                std::cout << "File changed: " << entry.filePath << std::endl;
                if (callback_) {
                    callback_(entry.filePath);
                }
            } else if (modTime > 0 && entry.lastModTime == 0) {
                entry.lastModTime = modTime;
            }
        }
    }
}
