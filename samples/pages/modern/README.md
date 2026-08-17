# Modern Cases

> Last updated: 2026-08-17; Applies to: 0.6.0-dev

These are standalone HTML/CSS review fixtures, not complete `.jfapp` packages.
They are intentionally split between ordinary supported patterns and explicit
degradation probes, so a visual reviewer can distinguish an engine regression
from a browser-only expectation.

| Fixture | Primary review purpose | Intentional non-default syntax |
| --- | --- | --- |
| `article_cards.*` | Parser recovery, common cards and bounded text layout | Optional HTML end tags, `picture`/`source`, `:where()` and a media query are degradation probes. |
| `app_shell.*` | Custom element boxes, popover/dialog markup and basic cards | `@container` and `:is()` are deferred-selector/query probes. |
| `search_home.*` | Compact search-style layout | See the fixture source for its documented CSS subset. |
| `text_wrap_balance.*` | Short heading balance across narrow and wide viewports | Uses only the bounded 2-4 line natural-wrap subset; longer text falls back to ordinary wrapping. |

Use the Core scope references, rather than browser rendering, as the expected
behavior for every marked probe. New capability fixtures must state their
expected fallback in this file and add a focused automated regression.
