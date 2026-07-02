#!/usr/bin/env python3
"""
一条龙语音交互 — 基础版（ASR → LLM → TTS）

用法:
    python scripts/full_pipeline.py

输入:
    文字 → 直接 LLM 对话
    r    → 录音 → ASR → LLM → TTS
    quit → 退出
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src import VoicePipeline, PipelineConfig


def main():
    cfg = PipelineConfig(
        wake_word="",             # 基础版关闭唤醒词
        llm_model="qwen2.5:1.5b",
    )

    vp = VoicePipeline(cfg)
    vp.initialize()

    print("=" * 60)
    print("  输入文字 → 直接 LLM 对话")
    print("  输入 r   → 录音 → ASR → LLM → TTS")
    print("  输入 quit  → 退出")
    print("=" * 60)
    print("🎉 就绪！\n")

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

        if cmd == "r":
            vp.process_voice()
        else:
            vp.process_text(cmd)


if __name__ == "__main__":
    main()
