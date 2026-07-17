#!/usr/bin/env python3
"""
Piper TTS 常驻服务。

支持两种音素转换模式，通过环境变量 PIPER_PHONEME_MODE 切换：
  - fast (默认): pypinyin 词典模式（~5ms），速度快但多音字可能不准
  - accurate:     g2pw BERT 模式（~2s），多音字消歧精准

格式:
  请求: 文本\n
  响应: [4 字节 PCM 长度 BE] [PCM int16 LE]，长度=0 表示错误
"""

import os
import sys
import struct
import json
import re

os.environ.setdefault("HF_ENDPOINT", "https://hf-mirror.com")

PHONEME_MODE = os.environ.get("PIPER_PHONEME_MODE", "fast")

# ── 从 Piper 复用常量 ───────────────────────────────

PINYIN_INITIALS = [
    "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
    "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
]


def _normalize_syllable(syl: str) -> str:
    """标准化拼音音节，同 g2pw 格式。"""
    m = re.match(r"^([a-züv:]+?)([1-5])$", syl)
    if not m:
        return syl
    base, tone = m.group(1), m.group(2)
    base = base.replace("u:", "v").replace("ü", "v")
    return base + tone


def _split_initial_final_tone(syl: str):
    """拆分拼音音节为声母、韵母、声调。"""
    m = re.match(r"^([a-zvü]+?)([1-5])$", syl)
    if not m:
        return "", "", None
    base, tone = m.group(1), m.group(2)

    ini = ""
    for cand in PINYIN_INITIALS:
        if base.startswith(cand):
            ini = cand
            break
    fin = base[len(ini):] if ini else base
    return ini, fin, tone


# pypinyin → Piper 的拼音映射修正
# pypinyin 用标准拼音 (ya, yao, wan)，Piper 用注音风格 (ia, iao, uan)
_ZY_TO_PIPER = {
    "ya": "ia", "yao": "iao", "yan": "ian", "yang": "iang",
    "ye": "ie", "you": "iu", "yong": "iong",
    "wa": "ua", "wai": "uai", "wan": "uan", "wang": "uang",
    "wo": "uo", "wei": "ui", "wen": "un", "weng": "ueng",
    "wu": "u", "yi": "i", "yin": "in", "ying": "ing",
    # yu 系列保留原样，让模型自己处理（避免 v 音素发音不准）
    # "yu": "v", "yue": "ve", "yuan": "van", "yun": "vn",
}

def _fix_syllable(syl: str) -> str:
    """修正 pypinyin 音节到 Piper 格式。"""
    m = re.match(r"^([a-züv:]+?)([1-5])$", syl)
    if not m:
        return syl
    base, tone = m.group(1), m.group(2)
    base = _ZY_TO_PIPER.get(base, base)
    return base + tone


def fast_phonemize(text: str) -> list:
    """用 pypinyin 做拼音转换，返回 Piper 兼容音素列表。"""
    from pypinyin import pinyin, Style

    # 获取每个字的拼音（TONE3 格式：ni3, hao3）
    pinyin_list = pinyin(text, style=Style.TONE3, errors="ignore")

    # 加载 Piper 的 phoneme_id_map 用来判断标点
    from piper.phonemize_chinese import PHONEME_TO_ID

    phonemes = []
    for (py,), ch in zip(pinyin_list, text):
        if py is None or py == ch:
            if ch in PHONEME_TO_ID:
                phonemes.append(ch)
            continue

        syl = _fix_syllable(_normalize_syllable(py))
        ini, fin, tone = _split_initial_final_tone(syl)
        if not fin:
            # 无法拆分的音节，直接整体添加（并在 map 中检查）
            if syl in PHONEME_TO_ID:
                phonemes.append(syl)
            else:
                # 尝试逐个字符添加
                for c in syl:
                    if c in PHONEME_TO_ID:
                        phonemes.append(c)
            continue
        if not ini:
            ini = "Ø"
        # 安全检查：只添加存在于音素映射表中的音素
        for p in [ini, fin, tone]:
            if p in PHONEME_TO_ID:
                phonemes.append(p)

    return [phonemes]


def main():
    model_path = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/pretrained_models/piper/zh_CN/zh_CN-xiao_ya-medium.onnx"
    )

    import piper
    from piper import phonemize_chinese as pc

    voice = piper.PiperVoice.load(model_path)

    if PHONEME_MODE == "accurate":
        # 使用 Piper 原生的 g2pw ChinesePhonemizer（精准但慢）
        print("[piper_server] Phoneme mode: g2pw (accurate)", file=sys.stderr, flush=True)
    else:
        # Monkey-patch: 替换为快速 pypinyin 模式
        class FastChinesePhonemizer:
            def __init__(self, download_dir=None):
                pass

            def phonemize(self, text):
                return fast_phonemize(text)

        pc.ChinesePhonemizer = FastChinesePhonemizer
        print("[piper_server] Phoneme mode: pypinyin (fast)", file=sys.stderr, flush=True)

    sample_rate = voice.config.sample_rate
    meta = {"sample_rate": sample_rate, "sample_width": 2, "channels": 1}
    print(json.dumps(meta), file=sys.stderr, flush=True)

    for line in sys.stdin:
        text = line.strip()
        if not text:
            continue

        try:
            chunks = []
            for chunk in voice.synthesize(text):
                chunks.append(chunk.audio_int16_bytes)

            pcm = b"".join(chunks)
            header = struct.pack(">I", len(pcm))
            sys.stdout.buffer.write(header)
            sys.stdout.buffer.write(pcm)
            sys.stdout.buffer.flush()

        except Exception as e:
            sys.stdout.buffer.write(struct.pack(">I", 0))
            sys.stdout.buffer.flush()
            print(f"[piper_server] Error: {e}", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
