#pragma once
/**
 * Embedding 引擎 — 文本 → 向量
 *
 * 支持两种后端:
 *   - "ollama":  调用 Ollama /api/embed 接口（需要 Ollama 运行）
 *   - "onnx":    本地 ONNX 模型推理（进程内，零网络开销）[Layer 4.2]
 */

#include <string>
#include <vector>
#include <memory>

class OnnxEmbedding;

class EmbeddingEngine {
public:
    EmbeddingEngine(const std::string& host, const std::string& model);
    static std::shared_ptr<EmbeddingEngine> create_onnx(const std::string& model_dir);
    ~EmbeddingEngine();

    std::vector<float> encode(const std::string& text) const;
    std::vector<std::vector<float>> encode_batch(const std::vector<std::string>& texts) const;
    int dimension() const { return dimension_; }

private:
    std::string host_;
    std::string model_;
    std::unique_ptr<OnnxEmbedding> onnx_;
    std::string backend_;
    mutable int dimension_ = 0;
};
