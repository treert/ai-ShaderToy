#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

/// FileWatcher 监控文件变化，用于 shader 热加载。
/// 使用轮询方式检查文件修改时间。
class FileWatcher {
public:
    using Callback = std::function<void(const std::string& filePath)>;

    FileWatcher();
    ~FileWatcher();

    /// 开始监控指定文件
    /// @param filePath 要监控的文件路径
    /// @param callback 文件变化时的回调
    /// @param intervalMs 轮询间隔（毫秒）
    void Watch(const std::string& filePath, Callback callback, int intervalMs = 500);

    /// 添加额外的监控文件
    void AddFile(const std::string& filePath);

    /// 停止监控
    void Stop();

    /// 是否正在监控
    bool IsWatching() const { return watching_.load(); }

private:
    void WatchThread();

    struct WatchEntry {
        std::string filePath;
        long long lastModTime = 0;
    };

    std::vector<WatchEntry> entries_;
    Callback callback_;
    int intervalMs_ = 500;
    std::atomic<bool> watching_{false};
    std::thread thread_;
    std::mutex mutex_;
};
