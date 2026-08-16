# Render Core 活动路线图

> 最后更新：2026-08-17；适用版本：0.6.0-dev

本文只安排 Render Core 工作，不安排 JellyFrame App Runtime、设备 port、launcher
策略、JerryScript 或 developer image。何时采用已发布的 Core 版本由其消费者决定。

## 当前候选线

`master` 是未签名的 `0.6.0-dev` 迁移线，Core ABI 为 `1`。它已有独立的
build/install CI、确定性源码归档，并完成以下低成本作者能力子集：

- LTR horizontal writing mode 的逻辑尺寸、间距与 inset 映射。
- 常用 flex/grid placement（`order`、`align-self`、`place-*`、有界 row）。
- 有界 sRGB `hsl()` / `hsla()` 与常用图片背景定位。
- 文字 `letter-spacing`、scalar-safe `overflow-wrap: anywhere`、ellipsis，及短文本
  自然换行的有界 `text-wrap: balance`。

`text-wrap: balance` 已在 `0fa5c41` 实现，并通过独立 CI 验证。只有带签名的 Core
release 被 Runtime dependency lock 消费后，它才成为正式能力；旧 Runtime 默认构建
不得提前宣称支持。

## 下一发布门槛

下一项 Core 工作是发布收束，而不是继续加入未经验证的 CSS 特性：

1. 复核候选源码、公开头文件与生成 profile 的改动。
2. 创建带签名的 annotated `0.6.0` tag，发布确定性源码归档及 SHA-256 sidecar。
3. 由 JellyFrame Runtime 更新精确 package/version/ABI/source lock，并运行
   installed-package 与 local-source-override 回归。
4. Device OS 在命名板卡 profile 中记录准确的 Runtime/Core provenance，之后才可以
   作出设备能力声明。

在门槛完成前，宿主可用 local source override 做跨仓库开发；生产消费者不得浮动依赖
此分支。

## 0.6.0 后的候选受理

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
