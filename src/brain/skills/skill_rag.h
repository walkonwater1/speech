#pragma once
/**
 * RAG 检索增强生成技能
 *
 * 学习要点:
 *   1. RAG = 检索(Retrieve) + 增强(Augment) + 生成(Generate)
 *   2. 流程: 用户问题 → embedding → 向量检索 → 拼接上下文 → LLM 生成
 *   3. 解决 LLM 的两大问题:
 *      - 知识截止日期（模型训练后的新知识）
 *      - 幻觉（没有依据时瞎编）
 *
 * 数据流:
 *   用户问 "公司年假政策是什么"
 *     → embedding("公司年假政策是什么") → [0.1, 0.3, ...]
 *     → VectorStore.search() → top-3 最相关文档片段
 *     → 拼接: "参考资料:\n[片段1]\n[片段2]\n\n用户问题: 公司年假政策是什么"
 *     → LLM 基于参考资料生成精确答案
 */

#include "skill_base.h"
#include "embedding_engine.h"
#include "vector_store.h"
#include <string>
#include <memory>

class RAGSkill : public Skill {
public:
    /// @param embed_engine  已初始化的 EmbeddingEngine
    /// @param docs_dir      知识库目录路径
    RAGSkill(std::shared_ptr<EmbeddingEngine> embed_engine,
             const std::string& docs_dir = "knowledge_base");

    bool match(const std::string& text) override;
    std::string execute(const std::string& text) override;
    std::string describe() const override {
        return "你可以查阅知识库。当用户问到知识库中可能有的内容时，参考检索结果回答。";
    }

    /// 加载/重新加载知识库文档
    void load_documents(const std::string& docs_dir);

    /// 已索引的文档片段数
    size_t chunk_count() const { return store_.size(); }

private:
    std::shared_ptr<EmbeddingEngine> embed_;
    VectorStore store_;
    std::string docs_dir_;
    bool loaded_ = false;

    /// 从目录加载所有 .txt 文件
    static std::vector<std::string> load_files(const std::string& dir);
};
