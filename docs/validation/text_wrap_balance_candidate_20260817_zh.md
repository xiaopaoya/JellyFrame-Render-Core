# 历史 Text Balance 候选证据

> 记录日期：2026-08-17；Core 源码 revision：`2df7ad7`；状态：已被当前分支取代

本文记录较早的候选线，仅用于保留来源信息。当前 `0.6.2-dev` 分支不包含该实现，本文不是
当前能力或 release 支持声明。

## 目的

在通过 signed Core release 与 Runtime dependency lock 接纳前，验证有界
`text-wrap: balance` 子集。本文不是硬件证据，也不改变 Runtime 默认 capability matrix。

## 证据

- Unit coverage：`jellyframe_render_core_tests` 覆盖 CSS cascade、`@supports`、普通换行
  fallback、有界 break unit、layout/layer 共用的行生成，以及 hostile measure callback 的算术边界。
- Benchmark：本机执行 `jellyframe_render_core_microbench 200`，得到
  `text_balance_layout=6.84 us`、`text_balance_layer=3.945 us`。它们只是桌面 host 的
  对比归因，不能外推为设备性能。
- Desktop source override build：JellyFrame Runtime 以
  `JELLYFRAME_RENDER_CORE_SOURCE_DIR` 指向该 Core checkout，仅构建
  `jellyframe_pseudo_browser`。
- Capture：以 `samples/pages/modern/text_wrap_balance.*` 为源夹具，生成 `round-300`
  (300x300)、`rect-320x240`、`rect-172x320`。三份最终 diagnostics 均为零，且无横向或
  纵向溢出；已目检 BMP 的截断、重叠和背景伪影。

## 复现

在 JellyFrame Runtime checkout 中以 local Core source override 配置，然后对上述成对样例
文件调用其 `jellyframe_pseudo_browser`，在三个 viewport 下添加 `--diagnostics-json`。该夹具
故意保持在文档定义的短文本、自然换行、二至四行范围内。

## 历史出口

当时源代码线的 candidate evidence 已完成，但没有进入当前 `0.6.2-dev` 实现。未来重新纳入时必须重新
执行当前候选门槛。
