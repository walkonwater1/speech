# 会话恢复指南

> 下次打开终端时，按这个步骤恢复工作环境。

## 一键恢复

```bash
cd /home/lixin/eir/lixin/ASR-LLM-TTS
source ~/miniconda3/etc/profile.d/conda.sh
conda activate chatAudio
python test_pipeline.py
```

## 当前进度

- [x] 环境搭建完成（conda + PyTorch CPU + 依赖）
- [x] SenseVoice Small (ASR) 已下载，RTF 0.048
- [x] Qwen2.5-0.5B (LLM) 已下载，回复 ~0.4s
- [ ] TTS 待解决（EdgeTTS 被墙，可用 Kokoro 替代）
- [ ] Qwen2.5-1.5B 待下载（更聪明的 LLM，需 ~3GB）

## 下一步可选任务

1. 下载 1.5B 模型：`HF_ENDPOINT=https://hf-mirror.com python -c "from transformers import AutoModelForCausalLM; AutoModelForCausalLM.from_pretrained('Qwen/Qwen2.5-1.5B-Instruct', torch_dtype='auto', device_map='cpu')"`
2. 安装 Kokoro TTS 解决语音合成
3. 参考 `LEARNING_ROADMAP.md` 的 Phase 3，开始 C++ 集成

## 推送到 GitHub

```bash
git push speech master:main
```

---

> 最后更新：2026-07-01
