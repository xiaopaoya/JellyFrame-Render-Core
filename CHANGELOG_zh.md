# 更新记录

## 0.14.0 / ABI11

- 普通块可通过显式 contextual inline API，将样式一致的文本节点和中性内联 span
  合成一段进行整形。
- 捕获字形边界和原始 UTF-8 区间并用于指针与滚轮事件派发。源文档变更或销毁后，
  捕获失效。
- 修正正值 descent 字体的上下文行高及基线。已支持的 Flex 换行行布局覆盖六种
  justify-content 值。
- 为普通块流增加默认关闭的十进制 CSS 计数器，并支持在生成内容中输出计数。

## 0.13.0 / ABI11

- 增加可选的 contextual Flex 行换行，支持确定宽高的 row 容器及固定尺寸的直接文本项。
  有界路径处理换行分组、行间距和交叉轴对齐；不支持的尺寸、装饰、增长/收缩、排序
  及 column 换行仍会拒绝。

## 0.12.1 / ABI11

- 在单一固定宽度的 Grid 列中，为 start 对齐且高度为 auto 的块级图像保留固有
  尺寸。适用于整数宽度，或无尺寸约束、边框及内边距的 auto 宽度。
- DOM 销毁观察器注册成功后才发布输入状态。注册失败时撤销新增的观察器，
  构造期间自动聚焦抛出异常时清理已注册的观察器。

## 0.12.0 / ABI11

ABI11 改变公开 `Node` 的内存布局，所有 Core 消费者都必须重新构建。

- 增加有界、默认关闭的追踪式计算样式复用，用于 Render Tree 构建。遇到不支持
  的选择器、超限快照、DOM/resolver/viewport 不匹配或错误建树调用时会拒绝复用。
- 追踪局部样式和结构变更，并在销毁后代节点前使文档结构失效，包括
  `set_text_content()`。
- 快照只能由对应的预备状态及其建树器返回的同一棵树完成提交。

## 0.11.0 / ABI10

所有 Core 消费者均需使用新的公开样式和文本布局缓存定义重新构建。CMake
安装包接受 0.11 版本系列。

- Core 标量文本支持有界的 CSS `word-spacing`，在布局、字形定位和已支持的
  固有尺寸查询中计入普通空格和不换行空格的额外间距。
- 支持仅含基本多文种平面汉字、普通空白和硬换行的段落固有高度查询。

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
