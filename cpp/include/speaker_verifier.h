#pragma once
/**
 * 声纹验证 (Speaker Verification)
 *
 * Python 对应: src/speaker.py → SpeakerVerifier
 * 依赖:      sherpa-onnx C API (speaker verification model)
 */

#include <string>
#include <memory>

struct SherpaOnnxSpeakerVerificationModel;

class SpeakerVerifier {
public:
    /// @param enroll_dir  注册声纹存储目录
    /// @param threshold   相似度阈值 (0~1)，低于此值判定为不同人
    SpeakerVerifier(const std::string& enroll_dir, float threshold = 0.35f);
    ~SpeakerVerifier();

    SpeakerVerifier(const SpeakerVerifier&) = delete;
    SpeakerVerifier& operator=(const SpeakerVerifier&) = delete;

    /// 加载模型
    bool initialize();

    /// 是否有注册声纹
    bool has_enrolled() const;

    /// 保存声纹注册音频
    /// @param wav_path 语音文件（建议 >3 秒）
    /// @return true = 注册成功
    bool enroll(const std::string& wav_path);

    /// 验证当前说话人
    /// @param test_wav 待验证语音文件
    /// @return true = 同一人
    bool verify(const std::string& test_wav);

    /// 状态描述
    std::string status_text() const;

private:
    std::string enroll_dir_;
    float threshold_;
    const SherpaOnnxSpeakerVerificationModel* model_ = nullptr;
    bool initialized_ = false;

    std::string enroll_path() const;
};
