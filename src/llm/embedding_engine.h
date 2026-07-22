#pragma once
/**
 * Embedding 引擎 — 文本 → 向量
 *
 * 调用 Ollama /api/embed 接口，将文本转为固定维度浮点向量。
 * 向量之间的余弦相似度表示语义相似程度。
 *
 * Qwen2.5 0.5B: embedding 维度 = 896
 * Qwen2.5 3B:   embedding 维度 = 2048
 *
 * 学习要点:
 *   1. embedding 是文本的「语义坐标」— 意思相近的文本向量距离近
 *   2. 这是 RAG 的基础：「找到和用户问题语义最接近的文档片段」
 *   3. 对比「关键字匹配」vs「语义匹配」:
 *      - 关键字: "下雨" 匹配 "下雨", 不匹配 "降水"
 *      - 语义:   "下雨" 和 "降水" 的 embedding 余弦相似度很高
 */

#include <string>
#include <vector>

class EmbeddingEngine {
public:
    /// @param host   Ollama 服务地址
    /// @param model  模型名（必须和 chat 用同一个模型，否则维度不一致）
    EmbeddingEngine(const std::string& host, const std::string& model);

    /// 将文本编码为向量
    /// @param text  输入文本
    /// @return embedding 向量（维度取决于模型）
    /// @throws std::runtime_error 网络错误
    std::vector<float> encode(const std::string& text) const;

    /// 批量编码（一次 HTTP 请求编码多条，比逐条调用快）
    std::vector<std::vector<float>> encode_batch(const std::vector<std::string>& texts) const;

    /// embedding 维度（首次调用后可用）
    int dimension() const { return dimension_; }

private:
    std::string host_;
    std::string model_;
    mutable int dimension_ = 0;
};
