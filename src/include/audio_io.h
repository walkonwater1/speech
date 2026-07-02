#pragma once
/**
 * 音频 I/O
 *
 * Python 对应: src/audio_io.py → AudioRecorder / AudioPlayer
 * 依赖:      系统命令 arecord / aplay (ALSA)
 *
 * 机器人集成时替换为硬件 SDK 的麦克风/扬声器接口。
 */

#include <string>

class AudioRecorder {
public:
    /// @param sample_rate  采样率 (Hz)，sensevoice 需要 16000
    explicit AudioRecorder(int sample_rate = 16000);

    /// 交互式录音：提示按 Enter → 录音 → 再按 Enter 停止 → 保存 WAV
    /// @param output_path  输出 WAV 文件路径
    /// @return true = 成功
    bool record(const std::string& output_path = "temp_recording.wav");

    /// 获取 WAV 文件时长 (秒)
    static float get_duration(const std::string& wav_path);

private:
    int sample_rate_;
};


class AudioPlayer {
public:
    /// 播放 WAV 文件 (阻塞到播放结束)
    static bool play(const std::string& file_path);
};
