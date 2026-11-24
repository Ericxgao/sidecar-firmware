# DN2–Sidecar bridge contract, existing wire

This document freezes the current compatibility boundary; it introduces no new
wire version, machine ABI or firmware storage layout. The golden events in
`fixtures/events.json` are original test data, shared by the DN2 C encoder,
the Pi C++ codec and the optional compiled ColdFire check.

## Event stream

DN2 sends complete SysEx packets beginning `F0 7D 44 32 version 01` and ending
`F7`. Interior bytes are seven-bit clean. Raw data is packed in groups of up to
seven bytes: a mask carries their high bits, followed by their low seven bits.
Unused mask bits must be zero. Multibyte raw fields are big endian.

| Raw field | Bytes | Meaning |
|---|---:|---|
| sequence | 2 | Export sequence, wraps at 65536; coalesced unsent edits do not consume it |
| tick | 4 | DN2 frame tick, diagnostic ordering; not an absolute USB sample deadline |
| track | 1 | Zero-based DN2 track, 0–15 |
| voice | 1 | Allocated native voice owner, 0–15; not the track or Pi cache slot |
| flags | 1 | STATE=1, ON=2, OFF=4, KILL=8, RESET=16 |
| note, velocity, level | 2 each | Full raw words; note/controls retain 1/256 precision; receiver normalizes velocity by 32512 |
| parameters | 22 or 32 | Eleven words in v1, sixteen words in v2 |
| engine ID | 0 or 32 | v1 implies `mega.perkons`; v2 carries the full canonical ID |

V1 is 37 raw bytes / 50 packet bytes. V2 is 79 raw bytes / 98 packet bytes.
Do not serialize the in-memory C event structure or depend on its padding.
The decoder rejects invalid framing, length, seven-bit packing, flag combinations,
track/voice range and ID encoding without altering the destination event.
Control-domain checks also occur when parameters reach the native voice adapter;
successful packet decoding alone does not authorize rendering arbitrary values.

IDs are 1–31 printable non-space ASCII bytes, then zero padding to 32 bytes;
leading dot, slash, backslash and consecutive dots are invalid. RESET in v2 has
an all-zero ID. RESET is exclusive; ON/OFF require STATE. Reserved flag bits
must be zero. A parameter position and full ID are persistent identities;
catalog ordering, displayed names and Pi loader slot numbers are not.

The Pi preserves note edges, applies the associated parameters before a hit,
rejects stale ownership events and silences on sequence discontinuity. Loading
an unavailable engine retains the newest state without replaying missed hits.
Plain KILL releases a part; STATE|KILL and RESET silence it while retaining the
reservation. Session teardown releases all participating parts.

## Session control and audio ownership

Session packets are 50 bytes with header `F0 7D 44 32 01 02`, command at byte 6,
a nonzero 28-bit token in bytes 7–10, track selector at 11, fixed marker 1 at 12,
return mode at 13, zero reserved bytes 14–48 and final F7. Commands are HELLO=0,
ACK=1, KEEP=2, CLOSE=3. Track selector 0–15 selects a track; 16 requests tracks
1–8 as eight independent parts. Return mode 0 is legacy stereo; 1 is native mono.

Takeover requires matching token, track selection and return mode, with ACK
followed by RESET. The lease is two seconds; Pi renews every 500 ms. Token is
stale-session isolation, not authentication. Unknown versions/modes must not
partially activate a session. A future capability exchange must be a compatible
addition; it is not present merely because this document exists.

Native returns are eight dry mono lanes through each assigned DN2 track's native
filter/AMP/pan/FX sends. Pan/stereo processing happens downstream. The native
return adapter owns its lane/frame mapping and stock DSP insertion sites;
plugins contain neither DN2 addresses nor USB routing. Codec/ownership evidence
does not establish physical latency or eight-engine worst-case audio timing.

## Metadata, selection and storage

UI requests/replies use message types 3/4 (v2, with v3 contextual metadata) and bounded
token/serial/revision transactions. The exact descriptor, enum and catalog byte
layouts are in [UI-PROTOCOL](UI-PROTOCOL.md). Current bounds: sixteen
controls, two SYN pages, 36 enum names, 32 installed catalog entries, eight Pi
machine slots. Installed is different from loaded/READY.

Metadata reads never evict engines. A load request may populate an unused cache
slot. Only an explicit confirmed selection commits an ID/defaults to the DN2
sound. Native MACHINE SEL already supports `YES:SWAP` for NO_ROOM plus a complete
installed-but-unloaded descriptor: confirmation releases the selected part's old
reservation through the ensuing committed state, then loads the new engine.
Other parts, including muted/stopped parts, retain their reservations.

The existing DN2 sound converter stores the full ID in reserved SYN words with
schema markers and CRC-15/CAN, separately from the modulated control frame.
Legacy sounds retain their Perkons identity. A damaged generic record must not
silently become another engine. The exact word/native-record binding belongs to
the supported OS adapter (`sound.c`, `src/dnfw/sidecar/machines.py`), not the wire.
Compiled converter evidence is separate from project save/reopen on hardware.

## Change and verification rules

- New compatible RMP plugin: install on Pi and rescan; no DN2 reflash or per-engine
  bridge changes. Keep ID and parameter meanings stable across compatible updates.
- New engine semantics: new ID or deliberate migration; never silently reuse a P number.
- New firmware base: port/verify its native bindings; do not guess shifted addresses.
- Routine codec check: `python tools/dn2/check_bridge_contract.py --dn2 PATH`.
  Add `--coldfire` for the small target codec compile/emulation. Both consume the
  same frozen packet fixtures; fixtures are not regenerated during a passing test.
- Existing `check_dn2.py --native-only` covers receiver, session and metadata
  behavior. DN2 capture/storage/UI/USB checks remain in its own `test_sidecar_*`
  files. Do not turn all hardware/stress checks into a mandatory plugin-build gate.
