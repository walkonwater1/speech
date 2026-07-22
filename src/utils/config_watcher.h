#pragma once
/**
 * 配置文件热监听器 (Layer 4.4)
 *
 * 使用 Linux inotify 监听配置文件变更，支持:
 *   - 自动检测: IN_CLOSE_WRITE 事件 → debounce → 回调
 *   - 手动触发: SIGHUP 信号（在 main/server_main 中处理）
 *
 * 用法:
 *   ConfigWatcher watcher("config.json", []{
 *       // 重新加载配置...
 *   });
 *   watcher.start();
 *   // ... 服务运行 ...
 *   watcher.stop();
 */

#include <string>
#include <thread>
#include <atomic>
#include <functional>

class ConfigWatcher {
public:
    /// @param file_path  要监听的文件路径
    /// @param on_change  变更回调（在 watcher 线程中执行）
    ConfigWatcher(const std::string& file_path,
                  std::function<void()> on_change);

    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    /// 启动后台监听线程
    void start();

    /// 停止监听
    void stop();

    /// 是否正在监听
    bool watching() const { return running_.load(); }

private:
    std::string file_path_;
    std::function<void()> callback_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    int inotify_fd_ = -1;
    int watch_fd_   = -1;

    /// 后台线程主循环: inotify 事件 → debounce → 回调
    void watch_loop();
};
