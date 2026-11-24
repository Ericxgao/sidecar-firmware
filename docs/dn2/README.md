# DN2 bridge review candidate

This source candidate adds a Digitone II host profile to Sidecar. Up to eight
independent parts use a shared manifest-driven adapter, with sixteen fractional
controls, native machine names/pages, session ownership and eight mono returns.
Compatible Pi plugins can appear without adding a per-engine DN2 definition.

The merge proposal is on `propose/dn2-native`, based on current `origin/dev`
(0.5.1.2). The original 0.5.1 extraction remains on `review/dn2-bridge`.
The update preserves upstream's pool-code helpers, voice report reset on
reconnect and USB timing diagnostics. This is not a firmware release or a claim
that this extracted tree has been booted on hardware.

This branch is an experimental personal-use integration shared for possible
upstream adoption. It is not an officially supported DN2 edition, and its author
does not promise ongoing rebases, releases or support. The DN2MK2 endpoint is a
separate contribution; users currently need both modified codebases. See the
[adoption discussion](ISSUE.md). Private engines are not needed for the public demo.

## Review boundaries

- Core loader: return retired executable/writable pages and support eight
  resident images on the Pi. Existing five-slot targets remain supported.
- Host bridge: event/session handling, cached metadata, explicit loading,
  independent part ownership, built-in synth adapter and DN2 USB audio.
- Diagnostics: pairing-gated RAM capture and focused offline checks.
- No external drum implementations, plugin-library manifests, source pins,
  samples, firmware images, device configuration or private prototype history.
- Display support is independent and is not part of this contribution.

The v1 identity `mega.perkons` is retained solely for legacy saved-sound/wire
compatibility. It does not provide that synthesizer or require its installation.
Tests of v2 use original synthetic IDs and metadata fixtures. Upstream's existing
engines and demo plugins are unchanged except for the MD metadata accessor.

## Focused checks

From this repository, with Python and clang/clang++ or compatible host tools:

```
python tools/dn2/check_dn2.py --usb
python tools/dn2/check_image_arena.py
python tools/dn2/check_bridge_contract.py --dn2 ../dn2mk2-bridge-public
```

The first command exercises codecs, parts, sessions, metadata, built-ins and the
real USB driver using Circle shims. The second checks both five/eight-slot maps
and allocator churn. The third compares frozen events against the actual DN2 C
encoder and Pi C++ decoder. Add `--coldfire --m68k-bin PATH` for the focused
ColdFire compiler/Unicorn check. No command flashes or communicates with a device.

The paired DN2 tests set `SIDECAR_BRIDGE_ROOT` to this checkout (the default
expects the sibling directory name shown above). They generate fixtures with:

```
python tools/dn2/make_metadata_fixtures.py
```

These RMP files contain **metadata and a null callback table only**. They are test
containers, not playable plugins; never install them. An actual plugin acceptance
demo can use an existing public Sidecar plugin such as SC Snare.

Build the unmodified, playable public demo with an existing AArch64 bare-metal
toolchain (no downloads or installation):

```
python tools/dn2/build_public_demo.py --a64-bin PATH_TO_TOOLCHAIN_BIN
```

The package and source/compiler receipt are written under
`build/dn2/public-demo/`. Set `SIDECAR_DEMO_PACKAGE` to the absolute path of
`sc_snare.a64.rmp` when running the paired DN2 metadata test. This tests real
package metadata, not audio rendering or live playback. Its ten controls also
exercise hidden unused SYN cells. The demo build passed with GNU Arm 13.3.rel1.

Checks write receipts under ignored `build/dn2/`. The public contract is
[event/session/storage](PROTOCOL.md) and [metadata](UI-PROTOCOL.md).

## Build and hardware limits

Recorded extraction check (2026-11-24): all host/USB checks above passed;
five/eight-slot allocator fixtures each completed 20,000 swaps; fourteen frozen
events matched host and compiled ColdFire encoders. Saved eight-part packets
from the paired DN2 candidate also passed the Pi session/receiver check, including
missing/reinstalled engines, reordered slots, reconnect and cancelled loading.
These results use synthetic providers and owner-supplied firmware inputs for
the DN2 side; they do not execute external drum DSP or qualify hardware timing.

The paired DN2 contribution is prepared on current dn2mk2 `main` (v0.72 plus
documentation, Elektron OS 1.11), on `propose/sidecar-native`. The verified v0.72
executable sources are unchanged. Both earlier review branches are preserved.
See the paired `docs/sidecar-review.md` for current image/routing evidence and
`docs/sidecar-proposal.md` for the unsent maintainer handoff. The two endpoints
share a protocol; their product release numbers need not match.

The normal `tools/run_portable_tests.sh` now includes the DN2 portable codec,
session, metadata and control checks with ASan/UBSan, plus the five/eight-slot
allocator checks. The existing Pi suite already exercises the real USB driver
through Circle shims. These checks need no DN2 firmware or companion checkout.
See [the draft PR body](PR.md) for submission scope and outstanding acceptance.

Current merge-proposal evidence (2026-11-24): the native portable phase completed
with `ALL PI PORTABLE TESTS PASS` on Ubuntu 24.04/WSL, GCC 13 and Node 22.
Thirteen Mac-library/Circle-dependent targets were explicitly skipped. The web
phase initially hit six CRLF-sensitive source-parser failures in the Windows
checkout. With canonical LF inputs, 101 of 102 passed; the remaining pairing
case failed its lockout assertion twice, then passed in isolation on both an
unmodified `origin/dev` export and the candidate. Its intermittent behavior is
recorded, not fixed or suppressed by this proposal. There was no final all-green
whole-run invocation; these are phased results.

The added DN2 phase passed separately with ASan/UBSan, and both allocator maps
passed 20,000 swaps. The Windows real USB driver/mirror checks, fourteen frozen
wire events and saved eight-part packet exchange also passed. GCC warnings in
unchanged upstream sources are reported using the normal portable runner's
policy, rather than promoted to errors by the new helper. Runtime assertions
and sanitizer failures still fail the check.

Use a Linux checkout with LF line endings for the Linux test command. The
source-parsing web tests assume canonical newlines. The shareable
[validation receipt](validation.json) lists commands, skips and unrun checks.

Use upstream's Pi build instructions and dependency pins. The Pi host sets
`SIDECAR_EIGHT_ENGINES`; the display driver is not required. `dn2=1` enables the
profile, with eight-part native session startup by default; `dn2auto=0` retains
manual/stock-MIDI startup. Native returns require matching DN2 firmware; an
unacknowledged session must not take ownership.

Offline checks do not qualify a new firmware image, USB/DAC latency, worst-case
eight-engine playback, or a real project filesystem save/reopen. Run those
checks on a future combined candidate before calling it a hardware release.
New Elektron OS versions need an explicitly supported DN2 binding. Capability
negotiation and full isolation of firmware-specific bindings remain follow-up
work; this extraction does not expand the existing wire/UI contract.
