/**
 * RAG 检索增强生成技能 — 实现
 */

#include "skill_rag.h"
#include "document_chunker.h"
#include "skill_utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include "logger.h"

RAGSkill::RAGSkill(std::shared_ptr<EmbeddingEngine> embed_engine,
                   const std::string& docs_dir)
    : Skill("rag")
    , embed_(std::move(embed_engine))
    , docs_dir_(docs_dir)
{
    load_documents(docs_dir_);
}

void RAGSkill::load_documents(const std::string& docs_dir)
{
    store_.clear();
    loaded_ = false;

    auto files = load_files(docs_dir);
    if (files.empty()) {
        std::cout << "   [RAG] 知识库为空: " << docs_dir << std::endl;
        set_enabled(false);
        return;
    }

    std::cout << "   [RAG] 正在索引 " << files.size() << " 个文档..." << std::endl;

    // 1. 切分所有文档
    std::vector<std::string> all_chunks;
    for (const auto& content : files) {
        auto chunks = DocumentChunker::split(content, 256, 32);
        for (auto& c : chunks) {
            all_chunks.push_back(std::move(c));
        }
    }

    if (all_chunks.empty()) {
        LOG_INFO("   [RAG] 文档内容为空");
        set_enabled(false);
        return;
    }

    // 2. 批量 embedding（一次 HTTP 请求，比逐条快 10x+）
    try {
        auto embeddings = embed_->encode_batch(all_chunks);

        // 3. 写入向量库
        store_.add_batch(all_chunks, embeddings);

        loaded_ = true;
        set_enabled(true);
        std::cout << "   [RAG] ✅ 已索引 " << all_chunks.size()
                  << " 个片段 (维度=" << embed_->dimension() << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "   [RAG] ❌ embedding 失败: " << e.what() << std::endl;
        set_enabled(false);
    }
}

bool RAGSkill::match(const std::string& text)
{
    if (!loaded_ || store_.size() == 0) return false;

    // 总是尝试检索（如果没搜到相关内容，向量搜索会返回空或低分结果）
    // 也可以通过关键字预过滤：
    static const std::vector<std::string> keywords = {
        "什么", "怎么", "如何", "为什么", "介绍", "说明",
        "政策", "规定", "流程", "文档", "知识库", "帮我看",
        "查一查", "有没有", "告诉我"
    };
    return contains_any(text, keywords);
}

std::string RAGSkill::execute(const std::string& text)
{
    if (!loaded_) {
        return "";
    }

    try {
        // 1. 用户问题 → embedding
        auto query_emb = embed_->encode(text);

        // 2. 向量检索 → top-3 相关片段
        auto results = store_.search(query_emb, 3, 0.3f);

        if (results.empty()) {
            return "";  // 没搜到相关内容
        }

        // 3. 拼接上下文
        std::ostringstream ctx;
        ctx << "以下是从知识库检索到的相关信息，请基于这些信息回答用户问题:\n\n";

        for (size_t i = 0; i < results.size(); ++i) {
            ctx << "--- 参考资料 #" << (i + 1)
                << " (相关度: " << (int)(results[i].score * 100) << "%) ---\n";
            ctx << results[i].text << "\n\n";
        }

        ctx << "请基于以上参考资料回答问题。如果资料不足以回答问题，请如实告知。";

        std::cout << "   [RAG] 检索到 " << results.size() << " 个相关片段 (top score: "
                  << (int)(results[0].score * 100) << "%)" << std::endl;

        return ctx.str();

    } catch (const std::exception& e) {
        std::cerr << "   [RAG] 检索异常: " << e.what() << std::endl;
        return "";
    }
}

std::vector<std::string> RAGSkill::load_files(const std::string& dir)
{
    std::vector<std::string> contents;

    // 简单的目录遍历（不依赖 C++17 filesystem，保证兼容性）
    // 读取 knowledge_base/ 下的 .txt 文件
    auto read_file = [](const std::string& path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::ostringstream oss;
        oss << f.rdbuf();
        return oss.str();
    };

    // 尝试常见的知识库文件
    std::vector<std::string> candidates = {
        dir + "/README.txt",
        dir + "/knowledge.txt",
        dir + "/faq.txt",
        dir + "/docs.txt",
        dir + "/info.txt",
    };

    for (const auto& path : candidates) {
        std::string content = read_file(path);
        if (!content.empty()) {
            std::cout << "   [RAG] 已加载: " << path << " ("
                      << content.size() << " 字符)" << std::endl;
            contents.push_back(content);
        }
    }

    return contents;
}
