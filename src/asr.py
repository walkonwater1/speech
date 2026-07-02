"""
语音识别引擎 (ASR)

C++ 对应: sherpa-onnx SenseVoice C API
    // 伪代码
    #include "sherpa-onnx/c-api/c-api.h"

    class ASREngine {
    public:
        ASREngine(const std::string& model_path);
        std::string transcribe(const std::string& wav_path);
    private:
        const SherpaOnnxOfflineRecognizer* recognizer_;
    };
"""

import os
import time
from funasr import AutoModel


class ASREngine:
    """SenseVoice Small 语音识别

    输入 WAV → 输出中文/英文/日文等多语种文本"""

    def __init__(self, model_path: str):
        """
        参数:
            model_path: SenseVoice 模型目录路径
        """
        self.model_path = os.path.expanduser(model_path)
        self.model = None

    def initialize(self):
        """加载 ASR 模型"""
        print("[ASR] 加载 SenseVoice Small ...", end=" ", flush=True)
        self.model = AutoModel(
            model=self.model_path,
            trust_remote_code=True,
            disable_update=True,
        )
        print("✅")

    def transcribe(self, wav_path: str) -> str:
        """
        语音 → 文字

        参数:
            wav_path: 音频文件路径
        返回:
            识别出的文本（空字符串表示未识别到语音）
        """
        t0 = time.time()
        res = self.model.generate(input=wav_path, language="auto", use_itn=False)
        text = res[0]["text"].split(">")[-1].strip()  # SenseVoice 输出格式: "<|zh|>文本"
        elapsed = time.time() - t0
        print(f"   [ASR] \"{text}\"  ({elapsed:.2f}s)")
        return text
