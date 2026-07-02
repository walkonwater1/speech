"""
声纹验证 (Speaker Verification)

C++ 对应: sherpa-onnx speaker verification C API
    #include "sherpa-onnx/c-api/c-api.h"

    class SpeakerVerifier {
    public:
        bool has_enrolled() const;
        bool enroll(const std::string& wav_path);
        bool verify(const std::string& test_wav);
    private:
        const SherpaOnnxSpeakerVerificationModel* model_;
        std::string enroll_dir_;
        float       threshold_;
    };
"""

import os
import shutil
from modelscope.pipelines import pipeline


class SpeakerVerifier:
    """CAM++ 声纹识别

    用法:
        sv = SpeakerVerifier(enroll_dir="~/speaker_voice/")
        sv.initialize()
        sv.enroll("my_voice.wav")        # 注册
        ok = sv.verify("test.wav")       # 验证
    """

    def __init__(self, enroll_dir: str, threshold: float = 0.35,
                 model: str = "damo/speech_campplus_sv_zh-cn_16k-common"):
        """
        参数:
            enroll_dir: 注册声纹存储目录
            threshold:  相似度阈值 (0~1)，低于此值判定为不同人
            model:      modelscope 模型名
        """
        self.enroll_dir = os.path.expanduser(enroll_dir)
        self.threshold = threshold
        self.model_name = model
        self.model = None

    # ── 初始化 ────────────────────────────────────────

    def initialize(self):
        """加载 CAM++ 模型（首次自动下载 ~28MB）"""
        print("[SV] 加载声纹模型 CAM++ ...", end=" ", flush=True)
        self.model = pipeline(
            task='speaker-verification',
            model=self.model_name,
            model_revision='v1.0.0',
        )
        os.makedirs(self.enroll_dir, exist_ok=True)
        print("✅")

    # ── 状态查询 ──────────────────────────────────────

    def has_enrolled(self) -> bool:
        """是否已有注册声纹"""
        if not os.path.isdir(self.enroll_dir):
            return False
        for f in os.listdir(self.enroll_dir):
            if f.endswith('.wav'):
                return True
        return False

    def status_text(self) -> str:
        """获取状态描述字符串"""
        if self.has_enrolled():
            return f"声纹已注册 ✅ ({self.enroll_dir})"
        else:
            return f"声纹未注册 ⚠️ 请说 'enroll' 注册"

    # ── 注册 ──────────────────────────────────────────

    def enroll(self, wav_path: str) -> bool:
        """
        保存声纹注册音频

        参数:
            wav_path: 待注册的语音文件（建议 >3 秒）
        返回:
            True  注册成功
            False 语音太短（<3 秒）或文件不存在
        """
        if not os.path.exists(wav_path):
            print("   [SV] ⚠️ 录音文件不存在")
            return False

        # 检查时长
        import wave
        with wave.open(wav_path, 'rb') as wf:
            duration = wf.getnframes() / wf.getframerate()

        if duration < 3:
            print(f"   [SV] ⚠️ 录音仅 {duration:.1f}s，声纹注册需要 3 秒以上")
            return False

        os.makedirs(self.enroll_dir, exist_ok=True)
        enroll_path = os.path.join(self.enroll_dir, "enroll_0.wav")
        shutil.move(wav_path, enroll_path)
        print(f"   [SV] ✅ 声纹注册完成 ({duration:.1f}s) → {enroll_path}")
        return True

    # ── 验证 ──────────────────────────────────────────

    def verify(self, test_wav: str) -> bool:
        """
        验证当前说话人是否与注册声纹匹配

        参数:
            test_wav: 待验证的语音文件
        返回:
            True  同一人
            False 不同人 / 未注册 / 验证失败
        """
        enroll_wav = os.path.join(self.enroll_dir, "enroll_0.wav")
        if not os.path.exists(enroll_wav):
            print("   [SV] ⚠️ 声纹未注册，请先注册")
            return False

        try:
            result = self.model([enroll_wav, test_wav], thr=self.threshold)
            score = result.get('scores', ['N/A'])[0] if 'scores' in result else 'N/A'
            is_same = result['text'] == "yes"
            status = "✅ 通过" if is_same else "❌ 拒绝"
            print(f"   [SV] {status} (分数: {score}, 阈值: {self.threshold})")
            return is_same
        except Exception as e:
            print(f"   [SV] 验证失败: {e}")
            return False
