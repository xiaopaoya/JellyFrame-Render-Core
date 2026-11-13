# Bounded shared inline paragraphs

The normal LayoutEngine path attempts one complete paragraph for an ordinary
Block containing nested neutral Inline spans, text and explicit br/wbr markers.
The TextSystem option activates this foundation. A single direct text child
keeps the established leaf cache contract. Member boundaries add no wrap cuts.
The collector also retains exact source-order proof for collapsible whitespace
text intentionally omitted by RenderTree. Missing non-whitespace sources,
generated nodes and incomplete render membership refuse the complete attempt.

## Current admission

This delivery accepts collapsing ASCII, per-owner registered font requests and
line-heights, start/center/end alignment, first-line indent, nowrap and anywhere
wrapping. Host alignment and wrapping policy remain paragraph-wide. Width
planning and glyph positions retain Q1000 precision, including
fractional containing widths. Placement uses the current integer box-origin
policy. Signed odd half-leading floors the first half and preserves line advance.
Neutral descendants have zero edges, automatic dimensions, static positioning,
full opacity, visible text, empty transforms and overflow. The bounded shared
paint subset also admits solid inline-scope backgrounds, zero-blur text shadows,
and the existing underline/line-through decoration flags; it excludes box
shadows, blurred text shadows, outlines, generated content and independent
stacking scopes.
Unsupported whole contexts continue through the established ordinary formatter.
Paragraph hosts undergoing temporary Flex/Grid/positioned sizing overrides or
mixed-size style materialization also use that formatter until their source-style
proof can be retained independently of temporary used sizing. Unmodified probes
and nested paragraphs with stable styles remain eligible. Committing ordinary
fallback retires any earlier shared probe bindings before changing geometry.

The implementation adapts 59b4bbf1 core_text_paragraph and its private line-plan
primitives. Font identities come from the current TextSystem family registry.
No legacy host measurement or font provider participates in this path.

## Ownership and consumers

TextLayoutCache gains one empty-by-default shared_ptr to an opaque private
binding. On Windows x64 Release the field adds 16 bytes to TextLayoutCache and
LayoutBox, making them 312 and 1320 bytes respectively at this delivery base.
The binding owns one immutable paragraph shared by its host and participating
boxes. It retains normalized/source ranges, exact source bytes, member topology,
style inputs, TextSystem instance/generation, final packets, line geometry and
separate per-line logical fragments and glyph ink bounds. Omitted whitespace
sources retain their DOM parent and child slot separately.

Preparation creates the complete draft and all bindings before publishing any
box or cache mutation. Failed allocation, admission or budget attempts preserve
all previous bindings and geometry. LayoutMutationJournal swaps the bindings
with the enclosing speculative geometry. Descendant rectangles and stored
relative geometry move together through the existing tree translation path.

The host emits the whole paragraph transactionally as typed PreparedText
commands with each source owner's color. Leaf caches are bindings into that
paragraph. A command budget admits the whole paragraph before emission. Live
paint and hit validate complete source/style/topology, relative geometry and
font identity. Dirty text/style reuse requires fresh shared layout. Any member
mutation includes both complete historical and current paragraph bounds in
damage. Frozen packet commands remain replayable after DOM mutation. Logical
per-line fragments drive hit testing, excluding scope-union gaps. Line extents
feed the existing scroll range and scroll-coordinate hit mapping.

The DOM and layout tree retain the existing lifetime contract. Old display
commands own packet bytes and can replay independently of later live source
validation while their registered font resources remain installed.

## Packet and ABI decision

The unpublished ABI4 candidate deliberately grows the installed layout option
and cache types. Consumers rebuild against the exact package source identity.
The text-system installed consumer exercises cache copying with the incomplete
private binding type and both packet variants.

JFTP capability version is now 2. Header and record sizes remain 32 and 52
bytes. Version 2 stores a signed Q1000 initial glyph X in header bytes 24 through
27, with cumulative glyph positions starting at that value. Header bytes 28
through 31 remain zero. Version 1 retains zero initial X and zero reserved bytes.
The encoder defaults to version 1 for zero origin. The decoder accepts both
versions and rejects unknown versions. TextPaint keeps its existing integer
origin and baseline payload. Fractional owner offsets travel in the packet.
Runtime consumers with a version-1-only capability gate require the coordinated
v2 compatibility update before adopting this package.

## Resource accounting

SharedInlineLayoutOptions defaults to 128 attempts, 64 KiB conservative charged
new payload per context, 256 KiB cumulative acquisitions per public layout call,
262144 source/planner work units, 2048 measurements and 262144 measured bytes.
Individual collectors additionally cap complete DOM membership at 128 nodes,
depth at 32 and source at 8192 bytes. The existing max_prepared_text_glyphs option
defaults to 128 for the complete paragraph. The paragraph line ceiling is 128.

Pass-local SharedInlineLayoutStatistics includes unsuccessful attempts and
speculative work. Rollback preserves those charges. Explicit owner, source,
span, line, packet, fragment and binding payload acquisitions share the operation
allowance. The capacity counter is conservative cumulative allocation payload,
including scratch and repeated acquisition. It measures neither process peak
heap nor the simultaneous live old/new trees. Allocator bookkeeping, debug
iterator proxies, caller-owned fonts, old frame packets and other pipeline
stages remain outside it. TextSystem measure/prepare use caller-owned glyph
storage and borrowed immutable font resources.

On the nested fixture the snapshot object is 416 bytes and each member object
is 224 bytes in Windows x64 Release. The test prints the conservative retained
capacity estimate, planner acquisitions and failed-allocation sweep count.
A binding object is 24 bytes plus its shared ownership control allocation.
An unbound box pays the 16-byte empty shared_ptr field. At 4096 boxes that field
cost is 64 KiB before sparse paragraph storage. Device RAM/performance requires
separate qualification against the exact consumer package.

## Inline image atomics

The shared paragraph collector accepts bounded `<img>` atomics after Core PR34
and PR35. An image must have `src`, no children, `srcset` or `sizes`; it uses
`object-fit: fill`, centered default object position and automatic image
rendering. Definite width and integral-pixel height are accepted directly.
Natural sizing also accepts intrinsic dimensions or one authored axis, plus the
currently supported min/max constraints, through the operation-owned metadata
memo. Fractional Q1000 width is retained; the final height must be integral.
Unknown or invalid metadata and unsupported sizing refuse the complete shared
paragraph attempt so the ordinary layout path remains responsible for it.

Atomic image placement participates in the common line plan and baseline, then
the layer builder emits one typed Image command between source-ordered text
fragments. Accepted frame commands retain the resolved resource for replay;
layout metadata is not queried again during paint or replay. These rules do not
extend shared-inline admission to replaced elements generally, `srcset`,
`sizes`, non-default object fitting/positioning, fractional image height,
decorated images or arbitrary image styling.

## Remaining finite migration stages

1. Extend complete source binding for generated/boxless contexts and preserved
   whitespace/tab/CRLF policies. Mixed registered font requests and per-owner
   line heights are integrated.
2. Define and adapt a Core-owned shaping/resource contract before integrating
   the historical shaped-inline source and frozen shaped packets. Its former
   host shape/metrics callbacks and JFGR font-instance packets are incompatible
   with the current Core-owned PreparedText paint contract.

The normal paragraph path, bounded inline-image stage, zero-blur text shadows,
per-line solid inline-scope backgrounds and the existing propagated
underline/line-through subset are integrated. Source-topology and shaped-inline
stages remain pending. The Style model has no overline or complete decoration
style storage, so those features remain outside this bounded subset.
