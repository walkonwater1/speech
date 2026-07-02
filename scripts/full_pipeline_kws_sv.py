#!/usr/bin/env python3
"""
一条龙语音交互 — 完整版（唤醒词 + 声纹 + 记忆）

用法:
    python scripts/full_pipeline_kws_sv.py

输入:
    文字    → 直接 LLM 对话（跳过唤醒词/声纹）
    r       → 录音 → ASR → 唤醒词 → 声纹 → LLM → TTS
    enroll  → 声纹注册（说 3 秒以上）
    clear   → 清空对话记忆
    quit    → 退出

C++ 集成: 所有功能模块在 src/ 下，每个 .py 文件头注释写了 C++ 对应实现。
"""

import sys
import os

# 把 repo 根目录加入 path（从 scripts/ 运行时也能 import src）
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src import VoicePipeline, PipelineConfig


def main():
    # ── 配置（修改这里即可）──
    cfg = PipelineConfig(
        wake_word="zhan qi lai",     # 唤醒词拼音，"" 关闭
        llm_model="qwen2.5:1.5b",   # Ollama 模型
    )

    # ── 初始化管线 ──
    vp = VoicePipeline(cfg)
    vp.initialize()

    print("=" * 60)
    print("  输入文字 → 直接 LLM 对话")
    print("  输入 r   → 录音 → 唤醒词 → 声纹 → LLM")
    print("  输入 enroll → 注册声纹")
    print("  输入 clear  → 清空对话记忆")
    print("  输入 quit  → 退出")
    print("=" * 60)
    print("🎉 就绪！\n")

    # ── 主循环 ──
    while True:
        try:
            cmd = input(">>> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not cmd:
            continue

        if cmd.lower() == "quit":
            print("👋 再见！")
            break

        if cmd.lower() == "clear":
            vp.clear_memory()
            continue

        if cmd.lower() == "enroll":
            vp.enroll_speaker()
            continue

        if cmd == "r":
            vp.process_voice()
        else:
            vp.process_text(cmd)


if __name__ == "__main__":
    main()
