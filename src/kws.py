"""
唤醒词检测 (KWS - Keyword Spotting)

C++ 对应: 拼音查找表 + 字符串匹配
    // 不需要模型的方案:
    //   1. ASR 输出文本 → 2. 汉字查拼音表 → 3. std::string::find(wake_word_pinyin)
    //
    // 拼音表是一个静态查找表:
    //   std::unordered_map<std::string, std::string> PINYIN_TABLE = {
    //     {"你", "ni"}, {"好", "hao"}, {"小", "xiao"}, {"千", "qian"}, ...
    //   };
    //
    // 也可以用 sherpa-onnx KWS 模型（需要单独训练唤醒词模型）

注意: 这个实现不需要 ML 模型，纯字符串处理。
      中文汉字 → pypinyin → 拼音字符串 → 子串匹配
"""

import re
from pypinyin import pinyin, Style


class WakeWordDetector:
    """基于拼音匹配的唤醒词检测

    用法:
        kws = WakeWordDetector("zhan qi lai")
        if kws.detect("小千站起来"):
            print("唤醒成功")
    """

    def __init__(self, wake_word: str = ""):
        """
        参数:
            wake_word: 唤醒词拼音，如 "zhan qi lai"
                       空字符串表示关闭唤醒词检测
        """
        self.wake_word = wake_word.strip()

    @property
    def enabled(self) -> bool:
        """是否启用了唤醒词"""
        return bool(self.wake_word)

    def detect(self, asr_text: str) -> bool:
        """
        检查 ASR 结果是否包含唤醒词

        参数:
            asr_text: ASR 识别出的中文文本
        返回:
            True  检测到唤醒词（或未启用唤醒词，直接通过）
            False 未检测到
        """
        if not self.wake_word:
            return True   # 没设唤醒词，直接通过

        pinyin_text = self._text_to_pinyin(asr_text)
        matched = self.wake_word in pinyin_text

        if not matched:
            print(f"   [KWS] 未检测到唤醒词 \"{self.wake_word}\" (识别: {pinyin_text})")
        else:
            print(f"   [KWS] ✅ 唤醒词检测成功")

        return matched

    # ── 静态工具方法 ──────────────────────────────────

    @staticmethod
    def _text_to_pinyin(text: str) -> str:
        """提取中文汉字 → 转拼音（无声调）"""
        chinese = re.findall(r'[一-龥]', text)
        if not chinese:
            return ""
        result = pinyin(''.join(chinese), style=Style.NORMAL)
        return ' '.join(item[0] for item in result)
