# Core DOM mutation observers

This independent migration targets Render Core 0.9.0-dev, engine ABI 4. It restores the Core-owned observer semantics used by the former ABI 9 source without inheriting the former package identity. On Windows x64 with Clang 22.1.8, standalone `sizeof(Node)` executables measured 192 bytes using the header from base `8922552c` and 208 bytes using this migration header (16 bytes added); consumers must rebuild with the matching public headers and package.

`Node::set_mutation_observer` replaces registrations throughout a subtree. It collects nodes before publishing, so allocation failure preserves every previous registration. `clear_mutation_observer` clears only matching callback/context pairs and is `noexcept`. Registration and clearing do not emit mutation callbacks. Destroy observers remain independent.

`mark_dirty` calls the actual mutation owner's observer synchronously after ancestor dirty propagation and the root generation increment. It provides the original flags and resulting generation. No-op writes are silent. A callback may change registrations or mutate other nodes; it must preserve the active owner's lifetime. Callback exceptions propagate after the mutation commits. Compound helpers can emit multiple callbacks for distinct committed writes.

Attachment replaces registrations throughout the inserted subtree, including independent overrides, with the parent's registration. A parent without an observer clears those overrides. Detachment clears only registrations matching the former parent, preserving independent overrides. Newly replaced text inherits the owner's registration; updating an existing sole text child reports that text child as owner.

Attachment preflights observer collection before tree insertion. Its unique pointer argument is owned by value: allocation failure can consume and destroy the supplied detached tree while leaving the parent tree and generation unchanged. No partial observer changes are published. Callback-induced sibling insertion cannot invalidate the reference returned for the original attached child.

Registration and normal matching clear use O(n) temporary pointers and linear traversal. Allocation failure during clear falls back to a constant-memory parent/sibling traversal; a deep valid tree is stack safe, while a wide tree can take O(n squared) time. Attaching to an unobserved parent also uses this allocation-free traversal to clear existing overrides, with the same worst-case wide-tree cost. The fallback requires valid parent and child links, as do the other DOM ownership operations.

The standalone observer regression covers exact owners, flags and generations; inherited and overridden registrations; text paths; detach and attach; no-op writes; registration and attachment allocation failures; allocation-free deep and wide cleanup; callback self-clear, reentrancy and committed exceptions; independent destroy observers; and compound form reset. The installed consumer links only the exported package target and checks ABI, profile and source identity.

## Integrated qualification

After rebasing onto the typed-command master c010fa83, the exact source hash is
bae7cab3a28d01006ace5ec9fe67ac824e882bb5cb398901a2f65f099933cdf3 with135 inputs.
Fresh Release full16/16 and cropped15/15 pass. AddressSanitizer and UBSan trap
Release each pass16/16. Exact full/cropped installed observer consumers each
pass1/1 with source/profile/ABI checks. Logs are retained under
E:/Temp/core-dom-observer-combined-qualification-20261110 and
E:/Temp/core-dom-observer-rebased-consumers-20261110.

The earlier independent-base report remains at
E:/Experiments/core-dom-observer-20261110/qualification.md. Device memory and
cycles for the additional16 bytes per Node and registration traversal still
require measurement on the selected product workload.
