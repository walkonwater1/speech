"""
管线配置

C++ 对应: 配置文件 / 启动参数结构体
    struct PipelineConfig {
        std::string asr_model_path;
        std::string llm_model;
        std::string system_prompt;
        int         tts_rate;
        std::string wake_word;
        std::string sv_enroll_dir;
        float       sv_threshold;
        int         sample_rate;
        int         max_rounds;
        int         max_tokens;
    };
"""

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class PipelineConfig:
    """语音交互管线全部配置，一处修改全局生效"""

    # ── ASR ────────────────────────────────────────────
    # SenseVoice Small 模型路径
    asr_model_path: str = "~/pretrained_models/SenseVoiceSmall/iic/SenseVoiceSmall"

    # ── LLM ────────────────────────────────────────────
    # Ollama 模型名（C++ 集成时对应 llama.cpp 的 GGUF 文件路径）
    llm_model: str = "qwen2.5:1.5b"
    system_prompt: str = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。"

    # ── TTS ────────────────────────────────────────────
    # pyttsx3 语速（词/分钟），C++ 集成时不用这个
    tts_rate: int = 200

    # ── 唤醒词 (KWS) ───────────────────────────────────
    # 拼音字符串，空字符串 = 关闭唤醒词
    wake_word: str = "zhan qi lai"   # "站起来"
    # wake_word: str = "ni hao xiao qian"  # "你好小千"

    # ── 声纹验证 (SV) ───────────────────────────────────
    # CAM++ 模型（modelscope），C++ 集成 → sherpa-onnx speaker verification
    sv_model: str = "damo/speech_campplus_sv_zh-cn_16k-common"
    sv_enroll_dir: str = "~/speaker_voice/"
    sv_threshold: float = 0.35   # 越小越严格

    # ── 音频 ────────────────────────────────────────────
    sample_rate: int = 44100

    # ── 对话记忆 ────────────────────────────────────────
    max_rounds: int = 10    # 保留最近 N 轮
    max_tokens: int = 512   # 总 token 上限（粗略）
