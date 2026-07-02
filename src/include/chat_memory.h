#pragma once
/**
 * 对话记忆管理
 *
 * Python 对应: src/memory.py → ChatMemory
 * C++ 实现:   std::deque<std::pair<std::string, std::string>>
 */

#include <string>
#include <deque>
#include <utility>

class ChatMemory {
public:
    /// @param max_rounds  最多保留 N 轮对话
    /// @param max_tokens  总 token 上限（粗略按字符数 * 4 估算）
    explicit ChatMemory(int max_rounds = 10, int max_tokens = 512);

    /// 记录一轮对话
    void add(const std::string& user_msg, const std::string& assistant_msg);

    /// 获取对话上下文（格式化文本）
    std::string get_context() const;

    /// 清空
    void clear();

private:
    std::deque<std::pair<std::string, std::string>> history_;
    int max_rounds_;
    int max_tokens_;

    void trim();
};
