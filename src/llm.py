"""
大语言模型推理引擎

C++ 对应: llama.cpp
    #include "llama.h"

    class LLMEngine {
    public:
        LLMEngine(const std::string& model_path, const std::string& system_prompt);
        std::string chat(const std::string& user_message,
                         const std::string& history_context = "");
    private:
        llama_model*   model_;
        llama_context* ctx_;
        std::string    system_prompt_;
    };

注意: 当前 Python 版通过 Ollama API 调用（Ollama 底层就是 llama.cpp），
      C++ 集成时直接链 llama.cpp，共用同一个 GGUF 模型文件。
"""

import time
import ollama


class LLMEngine:
    """Ollama LLM 推理（底层 llama.cpp）"""

    def __init__(self, model_name: str, system_prompt: str):
        """
        参数:
            model_name:    Ollama 模型名，如 "qwen2.5:1.5b"
            system_prompt: 系统提示词（人设）
        """
        self.model_name = model_name
        self.system_prompt = system_prompt

    def chat(self, user_message: str, history_context: str = "") -> str:
        """
        发消息给 LLM，获取回复

        参数:
            user_message:    当前用户消息
            history_context: 历史对话文本（可选）
        返回:
            LLM 回复
        """
        t0 = time.time()

        # 拼接历史 + 当前消息
        prompt = user_message
        if history_context:
            prompt = f"{history_context}\nUser: {user_message}\nAssistant:"

        messages = [
            {"role": "system", "content": self.system_prompt},
            {"role": "user", "content": prompt},
        ]

        try:
            response = ollama.chat(model=self.model_name, messages=messages)
            reply = response["message"]["content"].strip()
        except Exception as e:
            reply = f"[Ollama 错误: {e}]"

        elapsed = time.time() - t0
        print(f"   [LLM] \"{reply}\"  ({elapsed:.2f}s)")
        return reply
