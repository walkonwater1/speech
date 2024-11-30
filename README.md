# 241130-updata

## 新增声纹识别功能

设置固定声纹注册语音存储目录，如目录为空则自动进入声纹注册模式。默认注册语音时长大于3秒，可自定义，一般而言越长声纹效果越稳定。
声纹模型采用阿里开源的CAM++，其采用3D-Speaker中文数据训练，符合中文对话需求

## 新增自由定义唤醒词功能

通过SenceVoice的语音识别能力实现，为提高唤醒召回率，将汉字转为拼音进行匹配

## 新增对话历史内容记忆功能

通过建立user、system历史队列实现，可自由定义最大历史长度

对应脚本：

15.1_SenceVoice_kws_CAM++.py

Have fun! 😊

# 241123-updata

## 单模态语音交互

13_SenceVoice_QWen2.5_edgeTTS_realTime.py

## 音视频多模态语音交互

14_SenceVoice_QWen2VL_edgeTTS_realTime.py

[演示demo,b站] (https://www.bilibili.com/video/BV1uQBCYrEYL)

# 语音交互大模型/SenceVoice-QWen2.5-TTS

## 框架
SenceVoice-QWen2.5-CosyVoice搭建

此工程主代码来于[CosyVoice] (https://github.com/FunAudioLLM/CosyVoice)

在CosyVoice基础上添加[SenceVoice] (https://github.com/modelscope/FunASR) 作为语音识别模型

添加[QWwn2.5] (https://github.com/QwenLM/Qwen2.5) 作为大语言模型进行对话理解

## 3种语音合成方法

CoosyVoice推理速度慢，严重影响对话实时性，额外添加pyttsx3和edgeTTS

EdgeTTS实验过程出现链接错误问题，升级版本至6.1.17解决，无需科学上网

All dependencies are listed in requirements.txt, the interactive inference scripts are 10/11/12_SenceVoice_QWen2.5_xxx.py. 

Have fun! 😊
