# Render Core 活动路线图

> 最后更新：2026-08-30；适用版本：0.6.2-dev

本文只安排 Render Core 工作，不安排 JellyFrame App Runtime、设备 port、launcher
策略、JerryScript 或 developer image。何时采用已发布的 Core 版本由其消费者决定。

## 当前发布候选

带签名的 `v0.6.0` 已建立 Core ABI `1`。当前 `master` 是从 JellyFrame 主线
同步而来的未签名 `0.6.2-dev` 开发头（独立仓库合并提交 `769ec5d`），保留独立
build/install CI、确定性源码归档，并完成以下作者能力子集：

- LTR horizontal writing mode 的逻辑尺寸、间距与 inset 映射。
- 常用 flex/grid placement（`order`、`align-self`、`place-*`、有界 row）。
- 有界 sRGB `hsl()` / `hsla()` 与常用图片背景定位。
- 文字 `letter-spacing`、scalar-safe `overflow-wrap: anywhere` 与 ellipsis。

`text-wrap: balance` 曾在历史提交 `0fa5c41` 中探索，但当前 `0.6.2-dev` 实现和能力表
不包含它。旧 candidate evidence 仅作为历史上下文保留，不能作为当前分支的支持证据。
重新纳入前必须有新的提案、正/负行为测试、三个 target capture 以及明确的 Runtime 决策。

本 patch 还关闭了 HTML parser 的 depth budget 缺口：`max_depth` 计入合成 `document`
根节点，任何会超限的 child 会在进入 DOM 前被丢弃；固定 malformed-input corpus 保护该行为。

## 0.6.2-dev 开发门槛

下一项 Core 工作是候选能力评估，而不是隐式升级 Runtime：

1. 复核候选源码、公开头文件与生成 profile 的改动。
2. 对接受的能力发布经审查的 signed Core release、确定性源码归档及 SHA-256 sidecar。
3. 只有 installed-package 与 local-source-override 回归通过后，才由 JellyFrame Runtime
   更新精确 package/version/ABI/source lock。
4. Device OS 在命名板卡 profile 中记录准确的 Runtime/Core provenance，之后才可以
   作出设备能力声明。

在门槛完成前，宿主可用 local source override 做跨仓库开发；生产消费者不得浮动依赖
此分支。

## 0.6.1 后的候选受理

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
