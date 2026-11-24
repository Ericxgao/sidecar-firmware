# DN2 bridge review candidate

This source candidate adds a Digitone II host profile to Sidecar. Up to eight
independent parts use a shared manifest-driven adapter, with sixteen fractional
controls, native machine names/pages, session ownership and eight mono returns.
Compatible Pi plugins can appear without adding a per-engine DN2 definition.

The candidate starts at the Sidecar 0.5.1 development base. Updating to a newer
upstream revision is a separate integration task. This is not a firmware release
or a claim that this extracted tree has been booted on hardware.

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
