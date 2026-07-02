#pragma once
/**
 * 大语言模型推理引擎
 *
 * Python 对应: src/llm.py → LLMEngine
 * 依赖:      libcurl + nlohmann/json（调用 Ollama HTTP API，底层是 llama.cpp）
 *
 * 注意: Ollama 是本地服务，HTTP 延迟 <5ms，效果等价于直接链 llama.cpp。
 *        如果要完全嵌入（去掉 Ollama 进程），替换为 llama.cpp 的 llama.h API。
 */

#include <string>

class LLMEngine {
public:
    LLMEngine(const std::string& host, const std::string& model,
              const std::string& system_prompt);

    /// 发消息给 LLM，获取回复
    /// @param user_message     当前用户消息
    /// @param history_context  历史对话上下文（可选）
    /// @return LLM 回复文本
    std::string chat(const std::string& user_message,
                     const std::string& history_context = "");

private:
    std::string host_;           // Ollama 服务地址
    std::string model_;          // 模型名
    std::string system_prompt_;

    /// HTTP POST → Ollama /api/chat → 解析 JSON 回复
    std::string http_post(const std::string& request_json);
};
