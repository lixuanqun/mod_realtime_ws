# mod_realtime_ws 技术方案文档

本目录包含 `mod_realtime_ws` + `realtime-router` 端到端语音大模型实时对话方案的技术设计文档，用于评审。代码实现将在方案评审通过后进行。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [01-技术方案总览.md](./01-技术方案总览.md) | 背景、目标、术语、总体架构、模块职责、部署拓扑、技术选型 |
| [02-厂商协议分析.md](./02-厂商协议分析.md) | GLM-Realtime / 阿里云 ISI / 豆包(火山) 三家协议对比与适配差异 |
| [03-内部协议与事件定义.md](./03-内部协议与事件定义.md) | mod_realtime_ws ↔ realtime-router 内部协议、消息/事件、音频格式 |
| [04-时序图.md](./04-时序图.md) | 建链、对话、打断(barge-in)、Function Call、异常重连、挂机时序图 |
| [05-会话状态机与错误处理.md](./05-会话状态机与错误处理.md) | 会话状态机、重连退避、容灾、限流、背压 |
| [06-模块详细设计.md](./06-模块详细设计.md) | FreeSWITCH 侧 mod 设计、router 设计、配置、可观测性、里程碑 |

## 一句话概述

```
FreeSWITCH (PSTN/SIP/RTC)
        │  PCM 媒体流 (media bug)
        ▼
mod_realtime_ws  ──ws/grpc──►  realtime-router  ──ws──►  GLM-Realtime
 (媒体采集/下发)                (协议路由/适配/编排)  ──ws──►  阿里云 ISI
                                                      ──ws──►  豆包(火山)
```

- `mod_realtime_ws`：FreeSWITCH C 模块，负责通话媒体流的**上行采集**与**下行播放**，把语音与控制信令通过统一内部协议交给 router。
- `realtime-router`：独立服务，负责到各家语音大模型的 **WebSocket 连接管理、协议适配、会话编排、打断控制、路由与分发**。

## 评审关注点（建议）

1. 模块边界是否合理（VAD/重采样/编解码放在 mod 侧还是 router 侧）。
2. 内部协议选型（WebSocket vs gRPC-stream vs FreeSWITCH 原生 mod_audio_fork 风格）。
3. 统一会话抽象能否覆盖三家差异（尤其阿里云 ISI 是 ASR-only，需要叠加 TTS 才能端到端）。
4. 打断(barge-in) 的判定位置与时延预算。
5. 高并发下的连接池、背压与容量模型。
