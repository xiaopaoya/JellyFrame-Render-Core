# CSS Parser 裁剪范围

> 最后更新：2026-11-10；适用版本：0.9.0-dev / ABI4 候选版

最后对照 CSS Syntax Module Level 3、Media Queries 和浏览器 parser 源码结构的时间：
2026-06-16。

- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSS Syntax editor draft: https://drafts.csswg.org/css-syntax/
- CSS Conditional Rules Module Level 3: https://www.w3.org/TR/css-conditional-3/
- Media Queries Level 4: https://www.w3.org/TR/mediaqueries-4/
- Blink CSS parser sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/css/parser/
- WebKit CSS parser sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/css/parser

JellyFrame 应该能解析现代 CSS 而不发生灾难性失败，但不应该假装已经实现完整现代
cascade。Parser 接受常见语法，在 at-rule 和 declaration 边界恢复错误，并让不
支持的样式降级为统一 fallback，而不是把局部现代增强和旧布局行为混在一起。

## 视觉一致性策略

- 优先选择稳定 baseline styling，而不是局部现代 styling。
- 保留 declaration 顺序，让传统 fallback 生效：
  `color: #333; color: oklch(...);`。
- 不支持的 value 不能覆盖之前已经支持的 value。
- 不支持的增强 block 在 evaluator 存在前整体跳过。
- 坏规则不能破坏后续规则。

## 第一阶段：直接实现

ABI4 候选的 `border-radius` 支持每轴 1–4 个非负长度或百分比，可用 `/`
分隔横纵轴；百分比分别以盒宽、盒高为基准，所有角共享重叠缩减系数，整数
使用值向下投影。无效声明原子保留之前的两轴。函数值和单角 longhand 尚不支持。
非均匀轴的每角使用值上限为 127px；均匀轴保留正整数范围。超出表示范围时
同时拒绝两轴并报告诊断。椭圆阴影单独拒绝，不会连同盒子的背景一起省略。

- Comments。
- Qualified style rules。
- 按顶层逗号拆分 selector lists。
- Declaration blocks。
- 有序 declarations，包括重复属性。
- 解析阶段识别 `!important`。
- 在 selector 和 declaration 中处理 strings、escapes、functions 和 bracketed
  component values。
- 对 malformed declarations 和 malformed rules 做顶层错误恢复。
- 展开 `@layer` block。
- 当 prelude 为空、`all`、`screen`，或由 `screen`/`all` 加 `min-width`、
  `max-width`、`min-height`、`max-height` 条件组成时，展开 `@media` block。
  条件值支持 `px` 和无单位 px-like 数字；逗号分隔的 media list 按“任一匹配”处理，
  使用 parser options 中的紧凑 viewport 进行一次性判断。工具和 Win32 入口会在加载页面时把
  实际 render viewport 写入 parser options。可识别但不匹配的 media block 只作为 info 诊断，
  不作为 warning。
- 对保守的 declaration feature query 展开 `@supports` block。当前子集支持
  `(property: value)`、`not`、同质 `and`/`or` 链和括号；`selector()` 以及未知或
  不安全的 feature query 会求值为 false，让增强 block 干净降级。
- CSS custom property declarations，以及 style resolution 阶段的
  `var(--token)` / `var(--token, fallback)` 子集。Token 沿 DOM 路径继承，
  `:root` token 和 inline token 可用；无法解析的值不会覆盖之前的 fallback
  declaration。
- `Style` 用四个值语义背景图片层槽表示图像（槽 0 位于最上方），并使用一个共享的
  `background_color` 绘制在所有图像层下方。默认是一个 `none` 图像槽。`background-image`
  接受一到四项的有界逗号列表，每项为 `none`、受支持的 gradient 或包内绝对 URL。
  如 `url("/assets/image.bmp")` 的 URL 交由宿主图片 resolver；远程/data URL、相对路径、
  traversal、query 和 fragment 路径会被拒绝。`background` 接受相同图像项，并允许在最后一层
  指定共享颜色；纯色 shorthand 会设置共享颜色，并将图像列表重置为 `none`。
- 受支持的 gradient 包括带有界方向/角度的双色 `linear-gradient()`、两段进度环
  `conic-gradient()`，以及带中心或百分比定位的双色圆形 `radial-gradient()`。不支持的 stop、
  角度/方向、径向形状/位置或格式错误项会诊断，且不会替换先前有效的声明。
- `background-repeat`、`background-position`、`background-size`、`background-origin` 和
  `background-clip` 接受有界列表，并在图像列表上循环匹配。受支持的几何值包括 repeat/no-repeat
  组合、关键词/百分比位置、`cover`/`contain`/`auto` 或 `100% 100%` 尺寸，以及受支持的
  border/padding/content 盒子。shorthand 会重置图像几何；图像、几何和共享颜色 longhand
  独立参与 cascade，shorthand 颜色只作为所有图像层下方的共享颜色。无效声明保留之前的赢家。
  这不等同于完整 CSS background grammar，不支持任意 gradient stop、任意图像类型或无界列表。
- Type/class/id/attribute compound selectors、descendant 和 child combinators，
  以及 adjacent/general sibling combinators。
- 动态 pseudo-classes：`:hover`、`:active`、`:focus`、`:focus-within`、
  `:checked` 和 `:disabled`。
- `:is()` 和 `:where()` selector-list functions，支持当前 selector 子集；
  `:where()` specificity 为 0。
- CSS nesting 的显式单层 `&` 子集。父 declaration 与 nested rule 保留 source order；
  selector-list 展开上限为 16。隐式 nesting、多层 block 和 nested at-rule 输出
  `css-nesting-skipped`。
- 面向嵌入式应用子集的 UI 属性声明，包括 `outline`、`text-shadow`、`text-decoration`、`text-transform`、`letter-spacing`、`white-space: normal`/`nowrap` 及其 `text-wrap: wrap`/`nowrap` 别名、可继承的 `visibility: visible`/`hidden`（保留 layout 流，但 hidden 时不绘制且不作为命中目标）、`overflow-wrap: anywhere`、`aspect-ratio`、`gap`、
  物理 `margin-*`/`padding-*`/`border-*-width` longhands、`column-gap`、
  `row-gap`、`flex`、`flex-grow`、`flex-shrink`、`flex-basis`、有符号整数 `order`、
  `position`、`top`、`right`、`bottom`、`left`、带 `minmax()` 最小轨道的
  `grid-template-columns`、有界 `grid-template-rows` 固定/`1fr` 轨道、`repeat(N, 1fr)`、`repeat(N, minmax(0, 1fr))`、
  带最小轨道的 `grid-auto-rows`，有界正整数 `grid-column`/`grid-row` start/end/span placement，以及
  `object-fit`、关键词/百分比一二值 `object-position` 子集和 `image-rendering` 的
  `auto`/`pixelated`/`crisp-edges` 子集。
- `@keyframes` from/to 子集。命名 block 会保存 `from`/`to` 或 `0%`/`100%`
  declarations，供 animation timeline 使用。中间百分比会诊断并忽略，不做局部插值。

本地 0.9.0-dev/ABI4 候选版将 `flex-basis` 百分比保留为 Q1000 系数，
在容器主轴尺寸确定后求值；系数上限为 2,147,483.647%。自动高度纵向容器中，
无法确定的百分比使用现有内容探测，不取元素声明的高度；此时 `0%` 与 `0px`
不同。`flex` 支持单独基础尺寸及基础尺寸在前、连续伸缩因子在后的写法；
省略基础尺寸仍为 `0px`。声明与 `@supports` 共用语法校验。
混合百分比数学表达式和显式 `content` 关键字尚不接纳，完整内在 Flex 尺寸未完成验证。

有界单行候选路径也会在无需测量即可证明结果时应用自动最小尺寸规则：
现有滚动类 overflow、装饰边界处的确定尺寸上限，或可证明内容为空的普通盒子。
宽高比传递保留 Q1000 精度。一般内容自动最小尺寸仍需完整的内在内容查询；
未知内容继续回退，不假定为零。此处沿用主线耦合的 overflow 表示，不接纳独立双轴语义。

## 第一阶段：懒处理

- 不支持或复杂的 `@media`、不支持或复杂的 `@supports`、`@container`、`@scope`：
  整体跳过 block。可识别的 width/height `@media` 如果只是当前 viewport 不匹配，
  按正常分支选择处理。
- `@font-face`、`@page`、`@property`：识别 balanced block 边界，但暂不暴露给
  style resolution。
- 未知 at-rules：跳过 statement 或 balanced block。
- 暂时整体跳过 `:has()`、`::part()`、`::slotted()` 等不支持 selector 的完整规则。

## 明确尚未实现

- 完整 CSS token stream objects。
- 完整 selector parser。
- Cascade layers 和 layer ordering。
- 完整 media query evaluation，包括 `not`、range syntax、width/height 之外的
  media features，以及解析后随 viewport 变化动态重算。
- 完整 feature query evaluation，包括 `selector()`、font technology tests 和精确浏览器
  support 语义。
- 完整 custom property dependency graph、区分大小写的 custom property 名称、
  超出有界递归保护的循环处理，以及完整 invalid-at-computed-value-time 语义。
- 完整 CSS nesting semantics，包括隐式 nesting、nested at-rule 和多层递归 block。
- Shadow DOM selectors。
- 完整 Animation/keyframe model，包括中间关键帧、fill/play-state/composition 和
  layout 属性动画。
- 完整 grid value grammar、named/negative line、`grid-*-start/end` longhand、overlap resolution 和 dense packing。
- 完整 `object-position` 四值语法和长度偏移。
- Container query evaluation。该能力有价值，但会引入 style/layout 反馈环；
  在能可靠限制循环前刻意延后。

## 当前 parser 限制

直接使用 parser 时，`max_rules`、`max_declarations_per_rule`、
`max_input_bytes`、`max_nesting_expansion_bytes`、`max_selector_bytes`、
`max_at_rule_prelude_bytes` 和 `max_declaration_value_bytes` 使用 `0` 表示不限制。
生产宿主应通过 `HostBudgets` 提供非零上限；预算适配层会把为零的宿主预算规整为
有界的最小值，避免嵌入式运行时失去资源边界。

- `max_rules`：4096
- `max_declarations_per_rule`：256
- `max_nesting_depth`：8
- `flatten_layer_blocks`：true
- `parse_plain_media_blocks`：true
- `parse_supports_blocks`：true
- `media_viewport_width`：360
- `media_viewport_height`：240
