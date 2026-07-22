/**
 * 配置文件热监听器 — inotify 实现 (Layer 4.4)
 *
 * 仅支持 Linux（inotify 是 Linux 特有 API）。
 * 在非 Linux 平台上 start() 是无操作的。
 */

#include "config_watcher.h"
#include "logger.h"

#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>

// inotify 事件缓冲区大小（足够容纳多个事件）
static constexpr size_t EVENT_BUF_SIZE = 4096;
// 防抖延迟：编辑器保存时可能触发多次写入，等待写入稳定后再回调
static constexpr int DEBOUNCE_MS = 200;

ConfigWatcher::ConfigWatcher(const std::string& file_path,
                             std::function<void()> on_change)
    : file_path_(file_path)
    , callback_(std::move(on_change))
{
}

ConfigWatcher::~ConfigWatcher()
{
    stop();
}

void ConfigWatcher::start()
{
    if (running_.load()) return;

    // 创建 inotify 实例
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        LOG_WARN("[ConfigWatcher] inotify_init 失败: {} (将仅支持 SIGHUP 手动重载)",
                 std::strerror(errno));
        return;
    }

    // 监听文件：关注关闭写入（编辑器保存）和文件被移动/删除后重新创建
    watch_fd_ = inotify_add_watch(inotify_fd_, file_path_.c_str(),
                                   IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF);
    if (watch_fd_ < 0) {
        LOG_WARN("[ConfigWatcher] 无法监听文件 '{}': {} (将仅支持 SIGHUP 手动重载)",
                 file_path_, std::strerror(errno));
        close(inotify_fd_);
        inotify_fd_ = -1;
        return;
    }

    running_ = true;
    thread_ = std::thread(&ConfigWatcher::watch_loop, this);

    LOG_INFO("[ConfigWatcher] 🔍 开始监听配置文件: {}", file_path_);
}

void ConfigWatcher::stop()
{
    if (!running_.load()) return;

    running_ = false;

    // 唤醒阻塞在 read() 上的线程：向 inotify fd 写入会使其可读
    // 更简单的方法：关闭 fd，read() 会返回错误
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
        watch_fd_ = -1;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    LOG_INFO("[ConfigWatcher] 已停止监听");
}

void ConfigWatcher::watch_loop()
{
    char buf[EVENT_BUF_SIZE];

    while (running_.load()) {
        // 阻塞读取 inotify 事件
        ssize_t n = read(inotify_fd_, buf, sizeof(buf));

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式，暂无事件
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // fd 已关闭或其他错误 → 退出
            if (running_.load()) {
                LOG_WARN("[ConfigWatcher] inotify read 错误: {}", std::strerror(errno));
            }
            break;
        }

        if (n == 0) {
            // EOF — fd 可能已关闭
            break;
        }

        // 解析事件
        bool changed = false;
        for (ssize_t i = 0; i < n; ) {
            auto* event = reinterpret_cast<const inotify_event*>(buf + i);
            if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
                changed = true;
            }
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                // 文件被删除或移动 → 尝试重新监听
                LOG_WARN("[ConfigWatcher] 配置文件被删除/移动，尝试重新监听...");
                if (watch_fd_ >= 0) {
                    inotify_rm_watch(inotify_fd_, watch_fd_);
                    watch_fd_ = -1;
                }
                watch_fd_ = inotify_add_watch(inotify_fd_, file_path_.c_str(),
                                               IN_CLOSE_WRITE | IN_MOVED_TO |
                                               IN_MOVE_SELF | IN_DELETE_SELF);
                if (watch_fd_ < 0) {
                    LOG_WARN("[ConfigWatcher] 重新监听失败: {}", std::strerror(errno));
                } else {
                    // 文件可能已被重新创建（IN_MOVED_TO），触发重新加载
                    changed = true;
                }
            }
            i += sizeof(inotify_event) + event->len;
        }

        if (changed) {
            // 防抖：等待编辑器写入完成
            std::this_thread::sleep_for(std::chrono::milliseconds(DEBOUNCE_MS));

            // 清空防抖期间到达的后续事件（避免重复触发）
            while (true) {
                ssize_t drain = read(inotify_fd_, buf, sizeof(buf));
                if (drain <= 0) break;
                // 短暂等待更多事件
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            LOG_INFO("[ConfigWatcher] 📝 检测到配置文件变更，触发重载...");
            if (callback_) {
                callback_();
            }
        }
    }
}
