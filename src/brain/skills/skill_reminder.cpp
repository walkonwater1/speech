#include "logger.h"
/**
 * 提醒/计时技能 — 实现
 *
 * 使用 fork()+setsid() 创建完全独立的子进程，
 * 即使主进程退出，定时提醒也会触发。
 */

#include "skill_reminder.h"
#include "skill_utils.h"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>

// ── 关键词匹配 ──────────────────────────────────────────

bool ReminderSkill::match(const std::string& text)
{
    static const std::vector<std::string> keywords = {
        "提醒", "计时", "定时", "闹钟", "叫我",
        "分钟后", "秒后", "小时后", "倒计时",
        "设一个", "计时器", "时间到了"
    };
    return contains_any(text, keywords);
}

// ── 延迟解析 ──────────────────────────────────────────

int ReminderSkill::parse_delay_seconds(const std::string& text)
{
    // 尝试匹配 "N分钟后"
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '分' && i + 3 < text.size() &&
            text[i+1] == '钟' && text[i+2] == '后') {
            // 向前收集数字
            int num = 0, mult = 1;
            int j = (int)i - 1;
            while (j >= 0 && text[j] >= '0' && text[j] <= '9') {
                num += (text[j] - '0') * mult;
                mult *= 10;
                --j;
            }
            if (num > 0) return num * 60;
        }
    }

    // "N分钟"（无"后"）
    for (size_t i = 0; i + 4 < text.size(); ++i) {
        if (text[i] == '分' && text[i+1] == '钟') {
            int num = 0, mult = 1;
            int j = (int)i - 1;
            while (j >= 0 && text[j] >= '0' && text[j] <= '9') {
                num += (text[j] - '0') * mult;
                mult *= 10;
                --j;
            }
            if (num > 0) return num * 60;
        }
    }

    // "N秒后" / "N秒"
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '秒') {
            int num = 0, mult = 1;
            int j = (int)i - 1;
            while (j >= 0 && text[j] >= '0' && text[j] <= '9') {
                num += (text[j] - '0') * mult;
                mult *= 10;
                --j;
            }
            if (num > 0) return num;
        }
    }

    // "N小时后"
    for (size_t i = 0; i + 4 < text.size(); ++i) {
        if (text[i] == '小' && text[i+1] == '时' && text[i+2] == '后') {
            int num = 0, mult = 1;
            int j = (int)i - 1;
            while (j >= 0 && text[j] >= '0' && text[j] <= '9') {
                num += (text[j] - '0') * mult;
                mult *= 10;
                --j;
            }
            if (num > 0) return num * 3600;
        }
    }

    return 0;
}

// ── 执行 ────────────────────────────────────────────────

std::string ReminderSkill::execute(const std::string& text)
{
    return execute(text, nlohmann::json::object());
}

std::string ReminderSkill::execute(const std::string& text,
                                    const nlohmann::json& args)
{
    int seconds = 0;
    std::string message;

    // 优先使用 LLM 提取的参数
    if (args.contains("seconds") && args["seconds"].is_number()) {
        seconds = args["seconds"].get<int>();
    }
    if (args.contains("message") && args["message"].is_string()) {
        message = args["message"].get<std::string>();
    }

    // 降级：从文本中解析
    if (seconds == 0) {
        seconds = parse_delay_seconds(text);
    }
    if (message.empty()) {
        // 尝试从文本提取提醒内容（"提醒我XXX" → XXX）
        size_t pos = text.find("提醒我");
        if (pos != std::string::npos && pos + 9 < text.size()) {
            message = text.substr(pos + 9);
        } else {
            message = "时间到了！";
        }
    }

    // 安全限制
    if (seconds <= 0) {
        return "我没听懂要设多长时间的提醒，能再说一遍吗？比如「5分钟后提醒我出门」。";
    }
    if (seconds > 7200) {  // 最长 2 小时
        return "提醒最多只能设2小时以内哦。";
    }

    // ── 格式化为可读时间 ──────────────────────────────
    std::string time_str;
    if (seconds >= 3600) {
        time_str = std::to_string(seconds / 3600) + "小时"
                 + std::to_string((seconds % 3600) / 60) + "分钟";
    } else if (seconds >= 60) {
        time_str = std::to_string(seconds / 60) + "分钟"
                 + std::to_string(seconds % 60) + "秒";
    } else {
        time_str = std::to_string(seconds) + "秒";
    }

    std::cout << "   [Skill:提醒] 设定 " << time_str << " 后提醒: "
              << message << std::endl;

    // ── 创建后台子进程 ─────────────────────────────────
    // fork + setsid：子进程完全独立，主进程退出也不影响
    std::string notify_msg = message;
    int delay = seconds;
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：脱离终端，创建新会话
        setsid();

        // 再 fork 一次，彻底脱离（避免僵尸进程）
        pid_t pid2 = fork();
        if (pid2 > 0) {
            _exit(0);  // 中间进程退出
        }

        // 孙子进程：等待 → 发送通知
        sleep((unsigned int)delay);

        // 桌面通知
        std::string cmd = "notify-send '⏰ 提醒' '";
        cmd += notify_msg;
        cmd += "' -t 10000 2>/dev/null";
        (void)system(cmd.c_str());

        // 控制台 bell
        std::cout << "\a" << std::flush;

        // 音频提示（如果存在）
        (void)system("aplay -q /usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga 2>/dev/null");

        _exit(0);
    } else if (pid > 0) {
        // 父进程：等待中间进程退出（立即返回）
        int status;
        waitpid(pid, &status, 0);
    }

    std::ostringstream oss;
    oss << "好的，" << time_str << "后提醒你「" << message << "」。";
    return oss.str();
}
