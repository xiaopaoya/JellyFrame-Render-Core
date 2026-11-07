# Common computed-size storage

This is the storage phase on the unpublished Core 0.9.0-dev / ABI4 topic, after
c3e97cfa. Runtime mainline remains locked to the published 0.8.0 / ABI3 package.

The experiment's admitted Q1000 encoder is reused independently of CSS math,
positioned insets, gap residues and geometry feature switches. The new one-word
owner has seven slots for width/height/min/max and pixel Flex basis. Fractional
dimensions allocate an immutable shared block; integral-only styles leave it
empty. Copies share the block, mutations replace it and slot clearing releases
the last owner. The owner and scalar proxies are updated in that order so a
failed allocation cannot publish mismatched representations.

HTML dimensions and CSS literal pixel/percentage width/height/min/max now use
one computed-size publication helper. Integer, auto and supported min/max reset
overrides clear residues; invalid declarations preserve the prior value.
Existing relative-unit/function paths clear stale residues but still use their
legacy rounded results. Flex basis has a tested storage slot but its declaration
and layout paths are not yet migrated. Computed-size inheritance is not admitted
by this stage. Layout equality compares retained values independently of owner
identity, so a subpixel change cannot reuse layout solely because proxies match.

Host evidence:

- Core Release full/minimal configurations pass 6/6 CTest targets each.
- A dedicated failure-injection executable checks first allocation, replacement
  and partial-clear failures, copy/move isolation, independent logical equality,
  invalid values, decimal-half quantization and CSS override behavior.
- 10,000 integer publications allocate no precision blocks. The first fractional
  dimension allocates one block, and copying a Style does not allocate another.
- Windows Release Style grows from 776 to 784 bytes, an explicit one-pointer
  increase. The embedded guardrail changes by that measured amount, not by
  disabling the check. No MCU heap/Flash/frame-time qualification is implied.
- `html-replay.json` passes 244/244 scalar/ratio checks and 122/122 retained Q1000
  dimension checks against the unchanged 61-case corpus. The report still lists
  18 rounded scalar differences because the integer proxies are unchanged.
- The installed package consumer links/runs using exported headers/library and
  checks ABI4, Style size and fractional owner copy/read behavior.

This does not fix fractional layout geometry. General percentage bases, box
sizing, min/max, Flex/Grid solving, internal temporary overrides and geometry
rounding must migrate together in the following phases. In particular, direct
scalar mutations must not leave retained values in effect once layout starts
reading this owner. Existing 0.9 Runtime manifest/lock qualification remains
pending; the earlier ABI3 Runtime 56/56 result is not acceptance for this stage.
No remote publication or device flash was performed.
