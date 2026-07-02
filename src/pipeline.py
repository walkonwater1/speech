"""
语音交互管线编排器

C++ 对应: 你的机器人主控类
    class VoicePipeline {
    public:
        VoicePipeline(const PipelineConfig& cfg);

        // 初始化（加载所有模型）
        void initialize();

        // 文字输入：跳过 ASR/KWS/SV，直接 LLM → TTS → 播放
        std::string process_text(const std::string& text);

        // 语音输入：录音 → ASR → KWS → SV → LLM → TTS → 播放
        // 返回 LLM 回复文本（失败返回空字符串）
        std::string process_voice();

        // 声纹注册
        bool enroll_speaker();

        // 清空对话记忆
        void clear_memory();

    private:
        PipelineConfig  cfg_;
        ASREngine       asr_;
        LLMEngine       llm_;
        TTSEngine       tts_;
        WakeWordDetector kws_;
        SpeakerVerifier sv_;
        ChatMemory      memory_;
        AudioRecorder   recorder_;
        AudioPlayer     player_;   // 静态方法即可
    };

数据流:
    文字输入:  prompt ──→ LLM ──→ TTS ──→ 播放 → 更新记忆
    语音输入:  麦克风 ──→ ASR ──→ KWS ──→ SV ──→ LLM ──→ TTS ──→ 播放 → 更新记忆
"""

import os
from .config import PipelineConfig
from .asr import ASREngine
from .llm import LLMEngine
from .tts import TTSEngine
from .kws import WakeWordDetector
from .speaker import SpeakerVerifier
from .memory import ChatMemory
from .audio_io import AudioRecorder, AudioPlayer


class VoicePipeline:
    """完整语音交互管线

    用法:
        cfg = PipelineConfig()
        vp = VoicePipeline(cfg)
        vp.initialize()

        # 文字对话
        vp.process_text("你好")

        # 语音对话（含唤醒词+声纹）
        vp.process_voice()

        # 注册声纹
        vp.enroll_speaker()
    """

    def __init__(self, config: PipelineConfig | None = None):
        self.cfg = config or PipelineConfig()

        # 各模块（initialize() 后才就绪）
        self.asr      = ASREngine(self.cfg.asr_model_path)
        self.llm      = LLMEngine(self.cfg.llm_model, self.cfg.system_prompt)
        self.tts      = TTSEngine(self.cfg.tts_rate)
        self.kws      = WakeWordDetector(self.cfg.wake_word)
        self.speaker  = SpeakerVerifier(self.cfg.sv_enroll_dir, self.cfg.sv_threshold, self.cfg.sv_model)
        self.memory   = ChatMemory(self.cfg.max_rounds, self.cfg.max_tokens)
        self.recorder = AudioRecorder(self.cfg.sample_rate)

    # ════════════════════════════════════════════════════════════
    # 初始化
    # ════════════════════════════════════════════════════════════

    def initialize(self):
        """加载所有模型（阻塞，首次可能下载）"""
        self.asr.initialize()
        self.speaker.initialize()
        self.tts.initialize()

        # 显示状态
        features = []
        if self.kws.enabled:
            features.append(f'唤醒词: "{self.cfg.wake_word}"')
        features.append(f"声纹: CAM++ (阈值 {self.cfg.sv_threshold})")
        features.append(f"记忆: 最近 {self.cfg.max_rounds} 轮")
        features.append(f"LLM: {self.cfg.llm_model}")

        print(f"[4/4] Ollama: {self.cfg.llm_model} (后台就绪)")
        print(f"   特性: {', '.join(features)}")
        print(f"   {self.speaker.status_text()}")
        print()

    # ════════════════════════════════════════════════════════════
    # 文字输入
    # ════════════════════════════════════════════════════════════

    def process_text(self, text: str) -> str:
        """
        文字输入 → LLM → TTS → 播放 → 更新记忆

        返回:
            LLM 回复文本（失败返回空字符串）
        """
        # 1) LLM 推理（带对话记忆）
        context = self.memory.get_context()
        reply = self.llm.chat(text, context)

        if not reply:
            return ""

        # 2) 更新记忆
        self.memory.add(text, reply)

        # 3) TTS 合成 + 播放
        self._speak_and_play(reply)

        return reply

    # ════════════════════════════════════════════════════════════
    # 语音输入（完整链路）
    # ════════════════════════════════════════════════════════════

    def process_voice(self) -> str:
        """
        录音 → ASR → 唤醒词 → 声纹 → LLM → TTS → 播放 → 更新记忆

        返回:
            LLM 回复文本（在任一环节失败返回空字符串）
        """
        wav_file = "temp_recording.wav"

        # 1) 录音
        if not self.recorder.record(wav_file):
            return ""

        # 2) ASR
        prompt = self.asr.transcribe(wav_file)
        if not prompt:
            print("   ⚠️ 未识别到语音")
            self._cleanup(wav_file)
            return ""

        # 3) 唤醒词检查
        if not self.kws.detect(prompt):
            self._speak_and_play("请说出正确的唤醒词")
            self._cleanup(wav_file)
            return ""

        # 4) 声纹验证
        if not self.speaker.has_enrolled():
            self._speak_and_play("请先注册声纹，说 enroll 开始注册")
            self._cleanup(wav_file)
            return ""

        if not self.speaker.verify(wav_file):
            self._speak_and_play("声纹验证失败，我无法为您服务")
            self._cleanup(wav_file)
            return ""

        self._cleanup(wav_file)

        # 5) LLM
        context = self.memory.get_context()
        reply = self.llm.chat(prompt, context)
        if not reply:
            return ""

        # 6) 更新记忆
        self.memory.add(prompt, reply)

        # 7) TTS + 播放
        self._speak_and_play(reply)

        return reply

    # ════════════════════════════════════════════════════════════
    # 声纹注册
    # ════════════════════════════════════════════════════════════

    def enroll_speaker(self) -> bool:
        """录音 → 保存为声纹注册文件"""
        wav_file = "temp_enroll.wav"

        if not self.recorder.record(wav_file):
            return False

        if not self.speaker.enroll(wav_file):
            if os.path.exists(wav_file):
                os.remove(wav_file)
            return False

        self._speak_and_play("声纹注册完成！现在只有你可以命令我啦！")
        return True

    # ════════════════════════════════════════════════════════════
    # 工具
    # ════════════════════════════════════════════════════════════

    def clear_memory(self):
        """清空对话记忆"""
        self.memory.clear()
        print("   🧹 对话记忆已清空")

    # ── 内部 ────────────────────────────────────────────────

    def _speak_and_play(self, text: str):
        """TTS 合成 + 播放 + 清理临时文件"""
        tts_file = "temp_reply.wav"
        self.tts.synthesize(text, tts_file)
        print("   🔊 播放中...")
        AudioPlayer.play(tts_file)
        if os.path.exists(tts_file):
            os.remove(tts_file)

    @staticmethod
    def _cleanup(*paths: str):
        """删除临时文件"""
        for p in paths:
            if p and os.path.exists(p):
                os.remove(p)
