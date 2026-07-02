"""
语音合成引擎 (TTS)

C++ 对应: sherpa-onnx Kokoro / espeak
    // 方案 A: sherpa-onnx Kokoro (推荐，音质好)
    #include "sherpa-onnx/c-api/c-api.h"

    class TTSEngine {
    public:
        bool synthesize(const std::string& text, const std::string& output_path);
    private:
        const SherpaOnnxOfflineTts* tts_;
    };

    // 方案 B: espeak (轻量，音质一般)
    // espeak_Initialize() → espeak_Synth() → 写 WAV
"""

import time
import pyttsx3


class TTSEngine:
    """文字 → 语音文件（pyttsx3 / espeak 后端）"""

    def __init__(self, rate: int = 200):
        """
        参数:
            rate: 语速（词/分钟），espeak 默认 ~175
        """
        self.rate = rate
        self.engine = None

    def initialize(self):
        """初始化 TTS 引擎"""
        print("[TTS] 初始化 pyttsx3 (espeak) ...", end=" ", flush=True)
        self.engine = pyttsx3.init()
        self.engine.setProperty("rate", self.rate)

        # 可选：设置音色
        voices = self.engine.getProperty("voices")
        if voices:
            self.engine.setProperty("voice", voices[0].id)
        print("✅")

    def synthesize(self, text: str, output_path: str = "temp_reply.wav"):
        """
        文字 → WAV 文件

        参数:
            text:        待合成的文本
            output_path: 输出 WAV 路径
        """
        t0 = time.time()
        self.engine.save_to_file(text, output_path)
        self.engine.runAndWait()
        elapsed = time.time() - t0
        print(f"   [TTS] 合成完成 ({elapsed:.2f}s)")
