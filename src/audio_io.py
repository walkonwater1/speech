"""
音频录制 & 播放

C++ 对应: 硬件 SDK (如腾讯机器人音频接口)
    class AudioRecorder {
    public:
        bool record(const std::string& output_path, float* duration_sec);
    private:
        int sample_rate_;
    };

    void play_audio(const std::string& file_path);
"""

import time
import numpy as np
import sounddevice as sd
from scipy.io.wavfile import write
import pygame


# ═══════════════════════════════════════════════════════════════════════════
# AudioRecorder — 录音
# ═══════════════════════════════════════════════════════════════════════════

class AudioRecorder:
    """按 Enter 触发录音，再按 Enter 停止"""

    def __init__(self, sample_rate: int = 44100):
        self.sample_rate = sample_rate

    def record(self, output_path: str = "temp_recording.wav") -> bool:
        """
        交互式录音 → 保存 WAV 文件

        返回:
            True  录音成功
            False 录音失败（设备不可用等）
        """
        input("\n🔴 按下 Enter 开始录音...")
        print("   🎙️  录音中... 再次按下 Enter 结束")

        recording: list[np.ndarray] = []
        try:
            def callback(indata, _frames, _time_info, _status):
                recording.append(indata.copy())

            with sd.InputStream(samplerate=self.sample_rate, channels=1, callback=callback):
                input()    # 按 Enter 停止
        except Exception as e:
            print(f"   ❌ 录音失败: {e}")
            return False

        if not recording:
            print("   ⚠️ 未录到任何音频")
            return False

        audio_data = np.concatenate(recording, axis=0)
        write(output_path, self.sample_rate, (audio_data * 32767).astype(np.int16))
        duration = len(audio_data) / self.sample_rate
        print(f"   ✅ 录音完成 ({duration:.1f}s) → {output_path}")
        return True

    @staticmethod
    def get_duration(wav_path: str) -> float:
        """获取 WAV 文件时长（秒）"""
        import wave
        with wave.open(wav_path, 'rb') as wf:
            return wf.getnframes() / wf.getframerate()


# ═══════════════════════════════════════════════════════════════════════════
# AudioPlayer — 播放
# ═══════════════════════════════════════════════════════════════════════════

class AudioPlayer:
    """播放 WAV 文件（阻塞直到播放完毕）"""

    @staticmethod
    def play(file_path: str):
        try:
            pygame.mixer.init()
            pygame.mixer.music.load(file_path)
            pygame.mixer.music.play()
            while pygame.mixer.music.get_busy():
                time.sleep(0.1)
            pygame.mixer.quit()
        except Exception as e:
            print(f"   ❌ 播放失败: {e}")
