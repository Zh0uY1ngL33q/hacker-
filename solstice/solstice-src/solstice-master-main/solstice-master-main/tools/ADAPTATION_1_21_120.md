# 1.21.120.0 offline adaptation evidence

## Scope and validation boundary

Analysis uses the decrypted **virtual image** from
`Minecraft.Windows_1.21.120.0_20261027_005946.image` (226144256 bytes),
not the packed on-disk EXE. Addresses below are RVAs, not absolute addresses.
Capstone 5.0.7 decodes the instructions. Exact patterns are checked against
all three executable sections, not just the largest section.

An offline unique match and compatible instruction semantics are not an MSVC
build or a successful in-game test. The latter require the other machine.
RTTI and call-graph classification are not used to identify these targets.

## Input entries (2026-10-29)

### Keyboard_feed: wrong function -> state/event input entry

- Old call-site signature matches once at `0x3D596B`, but the call target
  `0x402380` is **not a keyboard routine**:
  `imul eax,[global],0x343FD; add eax,0x269EC3; store; shr eax,16;
  and eax,0x7FFF; ret` is the linear-congruential random-number sequence.
  The surrounding caller converts its return value to floats.
- New entry signature matches **once**, at `0x308B1E0`.
- `0x308B1E4`: `movzx eax,cl` bounds the key to a byte.
- `0x308B1FB`: `mov [r8+rax*4],edx` updates a global key-state table.
- The same routine records `cl` and the full `edx` into an event, then appends
  a `0x1C`-byte record to a global queue (`0x308B251`).
- Decode mode is now **Sig / 0**, not a rel32 call-site.
- Hook ABI is `uint8_t key, uint32_t state`; forwarding preserves the full
  state rather than reducing the native 32-bit value to a C++ bool.
- With no ImGui context or incomplete feature startup, pass through to the
  game instead of swallowing all key events. END does not dereference an
  unavailable ClientInstance or an unverified screen-name vtable slot.

### MouseDevice_feed: 3 generic prologues -> unique input serializer

- Old prologue matches at `0xCED950`, `0x346EEB0`, `0x86002C0`.
  Applying `rel(1)` to `48 8B C4 48 89 ...` treats opcode bytes as a signed
  displacement, producing an out-of-image target and
  `MH_ERROR_NOT_EXECUTABLE`. Simply changing its decode mode would still
  choose the wrong first match.
- New longer entry signature matches **once**, at `0x346EEB0`.
- The target consumes `this` in RCX, signed action/button byte DL, unsigned
  state byte R8B, X in R9W, and Y/dX/dY plus a flag from stack arguments.
- `0x346EF00..0x346EF27` builds the matching mouse-event layout;
  `0x346EF40` advances its queue by `0x14` bytes.
- `0x346EF51` handles action 4 (wheel); `0x346EF88..0x346EFE9`
  updates button state, absolute coordinates, and accumulated relative motion.
- Decode mode is now **Sig / 0**. Hook state width is uint8_t, matching R8B.

### Runtime checks still required

1. Build the new HEAD with the existing MSVC/CMake toolchain.
2. Verify real key and mouse events reach the new entries, game input remains
   functional without a renderer, and END still requests unload.
3. GUI display is **not** claimed fixed by the input changes: the latest
   available log has no D3D Present callback. Rendering is a separate blocker.
4. Do not count a reduced ambiguous-signature count as proof of GUI functionality.

## Client map: resolve the correct map and stop rejecting its sentinel children

- Old `MinecraftGame_mClientInstances` pattern matches at `0x1A3CA3A`
  (field `0xB60`) **and** `0x621A01A` (unrelated field `0x300`).
- Extended pattern matches only `0x1A3CA3A` and still extracts `0xB60`.
  It includes the node+`0x28` load and virtual call at byte displacement
  `0x118` (getLocalPlayer index 35), not just generic tree traversal.
- Crucial traversal correction: `0x1A3CAA8..0x1A3CACD` repeatedly loads
  `_Left` and immediately reads that child's `+0x19` isnil byte. Missing
  children therefore point to the **non-null head sentinel**. The old
  `first->_Left == 0` condition in both scanner and cached-map reader
  rejects real MSVC trees. Both now require `first->_Left == head`.
- Remove the speculative node+`0x20` value fallback. The image explicitly
  consumes node+`0x28`; treating key/padding as a shared_ptr is not supported
  by this build's layout.
- This proves a traversal defect and corrects a two-hit offset signature;
  it does not prove a runtime client has been found. Remaining game/cache
  validation and menu scanning policy are audited separately.

## Consistent game and cache validation

- `MinecraftGame` is no longer required to begin with a vtable in its SDK
  entry/access paths. `isPlausibleGame` validates the known client-map chain,
  including client/control-block vtables and the known MinecraftSim member.
- Remove the independent SDK client/game caches and abandoned MainView path;
  the locator owns caching, rechecking actual map membership on cached returns.
- The cached-map reader now checks map size, both shared_ptr pointers and Sim;
  it cannot bypass the stronger checks performed by a full scan.
- The full scan no longer publishes a client whose Sim chain did not validate.
  A later Sim-validated candidate can replace the first eight lookalikes.
- `getPrimaryClientInstance` uses the same guarded raw reader instead of
  copying/destroying a game-owned std::map from injected code.
- Runtime tests required: find client in a world, leave/re-enter, ensure stale
  caches are discarded, and unload. Menu-only discovery and scan latency remain
  unresolved; the existing empty-map heuristic is not proof of game identity.

## Three more invalid function-address decoders

### ContainerScreenController_tick

- The existing long signature is unique at `0x1E26600`. It starts with
  `mov rax,rsp; mov [rax+0x10],rbx; ...`, not `call rel32`.
- Old `rel(1)` gives RVA `-0x74D4D570`; corrected `Sig / 0` keeps the actual
  function entry. No pattern drift or guessed fixed RVA is introduced.
- The hook passes through while `Actor_mContainerManagerModel` is unresolved
  instead of reading a vtable as that member. Container feature behavior is
  still pending runtime verification; guarding it is not a recovered offset.

### GameMode_getDestroyRate

- Generic prologue matched `0x5BF54B0` and unrelated `0x8628110`; rel(1)
  decoded the prologue as an address instead of using either entry.
- Extend through the `[this+8]` player load, direct call, global load, and
  XMM0-to-XMM6 result preservation. New entry pattern is unique at `0x5BF54B0`.
- The function consumes the incoming block argument through its first call
  and applies player-state/effect logic to a floating-point result. The other
  prologue candidate instead allocates/constructs a `0x3B0`-byte object.

### GameMode_baseUseItem

- Two old sites (`0x39679D1`, `0x3968295`) start with `mov rcx,rax`; their
  actual calls are at +3, not +0. **Both** call `0x5BEC700`.
- Use a unique entry pattern at that common target instead of a generic
  mov-plus-call pattern with `rel(1)` pointing inside `mov`.
- Target consumes `this` in RCX and item argument in RDX, accesses player at
  `this+8`, constructs the use-item event, and returns a byte-valued result.
- These are offline address-decoding repairs, not a claim that all block or
  item-use behavior has passed in-game testing.

## Runtime result: 20c1d49 (2026-10-29)

- User build reaches client discovery: game/client/local-player pointers are
  non-null; scan examines 38 MB in **51 ms**, with Sim validation successful.
- Keyboard, mouse, and container tick detours are created at the recovered
  entries. Actual callbacks are not yet validated: init crashes earlier.
- New crash is `PacketSendHook::init`, dereferencing
  `ci->getPacketSender()->vtable[2]` after the client became available.
- `ClientInstance_mPacketSender` is still ambiguous. The first runtime hit
  at RVA `0x218374B` is `mov rax,[rax+0x228]` after loading a vtable; other
  hits load virtual slot `0x9A0`, and one is an unrelated short displacement.
  None proves a member offset on ClientInstance. Do **not** replace it with
  0x228, 0x9A0, or a guessed nearby getter's offset.
- Until the actual field is recovered, reject non-unique sender extraction;
  init checks the sender/vtable/target before installing its detour. Native
  sending remains unhooked, and synthetic sending skips an unavailable sender.
- This is a crash containment, **not** a recovered sender offset or completed
  packet feature. Sender recovery and in-game testing remain open.
