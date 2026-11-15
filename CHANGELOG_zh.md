# 更新记录

## 0.9.0 / ABI4

ABI4 与 ABI3 存在源码和二进制兼容性断裂。接入前需重建所有 Core 消费者，
并核对具体 package profile 与 source manifest。

### 破坏性变更

- 移除由宿主提供的文本测量、行度量、换行和字形绘制回调，包括
  `TextMeasureProvider`、`TextPainter`、range/family helper，以及位图字体
  测量/绘制 helper。Core 根据注册字体资源负责文本选择、度量、准备与栅格化。
- 移除由 provider 驱动的布局缓存身份和重绘/复用 hook；宿主改用 Core-owned
  文本与布局契约，不再提供平行的文本策略。
- 将扁平 display-command 绘制字段改为按命令类型区分的 typed payload；调用方
  必须按当前命令类型访问对应 payload。

### 新增与扩展

- 为 inline 文本、原生表单控件及 select 弹层选项提供 Core-prepared text packet，
  并支持有界 ellipsis 与共享段落。
- 增加水平/垂直独立圆角、多层背景、类型化图片资源元数据和受 feature gate
  约束的 natural-image sizing API。
- 保留确定性源码归档、source manifest 和生成的 feature profile 作为 package
  identity 的验证依据。

支持的 CSS 子集和明确排除项仍以 Core reference 为准。ABI4 不代表完整浏览器
CSS 或 shaping 兼容性。
