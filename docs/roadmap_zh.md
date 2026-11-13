# Render Core 活动路线图

> 最后更新：2026-11-13；适用版本：0.9.0-dev / ABI 4 验收线

本文只安排 Render Core 工作，不安排 JellyFrame App Runtime、设备 port、launcher
策略、JerryScript 或 developer image。何时采用已发布的 Core 版本由其消费者决定。

## 当前开发线

带签名的 `v0.8.0` / ABI `3` 是 Runtime 当前生产依赖。Core `master` 为
`0.9.0-dev` / ABI `4`，提交 `076b8b0f`；规范化 source manifest 为
`5cf5b2e2dd3b9dde6ac8268514f2addd033ddd69f09809b07f6601e49c53c714`（111 个
manifest 条目）。ABI4 仍处于验收轨道，尚不是签名发布的 Runtime 依赖。Runtime
installed consumers 覆盖 full/cropped profile、frame v9/v10 与 generated-text
replay；standalone archive 和 consumer 门槛由 CI 验证。签名 ABI4 release 与命名
Device OS 镜像验收仍未完成。当前开发线包括：

- LTR horizontal writing mode 的逻辑尺寸、间距与 inset 映射。
- 常用 flex/grid placement（`order`、`align-self`、`place-*`、有界 row）。
- 有界 sRGB `hsl()` / `hsla()` 与常用图片背景定位。
- 文字 `letter-spacing`、scalar-safe `overflow-wrap: anywhere` 与 ellipsis。
- 有界 dirty-region 与圆角光栅热路径、缓存文本布局和 viewport-aware 单位。
- command owner/span trace 插桩和 Windows CPU2D 对比 workload。

`text-wrap: balance` 曾在历史提交 `0fa5c41` 中探索，但当前开发实现和能力表
不包含它。旧 candidate evidence 仅作为历史上下文保留，不能作为当前分支的支持证据。
重新纳入前必须有新的提案、正/负行为测试、三个 target capture 以及明确的 Runtime 决策。

本 patch 还关闭了 HTML parser 的 depth budget 缺口：`max_depth` 计入合成 `document`
根节点，任何会超限的 child 会在进入 DOM 前被丢弃；固定 malformed-input corpus 保护该行为。

## 下一次发布门槛

下一项 Core 工作是有界 shaping 数据原型，而不是隐式升级 Runtime。现有
`JFFONT2` 仍是 scalar bitmap 数据；原型必须以产品 Noto Sans SC 源字体为 shaping
输入，并与独立 shaping oracle 对比已准入 lookup 的行为。完成正确性及 ESP32
内存/工作量测量前，不改公开 API 或默认行为。

ABI4 发布与采用仍按顺序经过以下门槛：

1. 复核候选源码、公开头文件与生成 profile 的改动。
2. 对接受的能力发布经审查的 signed Core release、确定性源码归档及 SHA-256 sidecar。
3. 只有 installed-package 与 local-source-override 回归通过后，才由 JellyFrame Runtime
   更新精确 package/version/ABI/source lock。
4. Device OS 在命名板卡 profile 中记录准确的 Runtime/Core provenance，之后才可以
   作出设备能力声明。

在门槛完成前，宿主可用 local source override 做跨仓库开发；生产消费者不得浮动依赖
此分支。

## ABI4 验收期间的候选受理

每个新能力必须有作者可复现的需求和有界提案。接受后需要正/负行为测试、三个 target
desktop capture、capability/diagnostic/recipe 更新；触及 layout 或 paint 时还需 hot-path
benchmark。

`font-style` 不作为只解析、不生效的低成本特性。正确实现要求 text measurement、paint
和全部 host adapter 共享版本化的 text-style contract，并给 bitmap font 定义 fallback。
因此它是 release 之后的候选，而不是静默忽略的 declaration。

## 明确延后

不将 container queries、`:has()`、复杂 grid/subgrid、filter、backdrop-filter、Shadow
DOM、Worker、iframe、完整 SVG/video、浏览器字体加载或复杂文字 shaping 作为默认 `0.6`
范围。retained replay、framebuffer reuse 与 tile/scanline renderer 也必须作为独立提案，
满足 memory、pixel correctness 与 target telemetry 门槛后才能进入实现。
