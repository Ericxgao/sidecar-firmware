# Native DN2 metadata transport

Bounded manifest-driven metadata, selection and recall; no per-engine DSP in DN2.

## Requests and replies

All interior bytes are seven-bit. Multi-byte integers here are big-endian groups
of seven bits. Both token and serial are nonzero 28-bit values. The session token
is correlation, not authentication. Every new request increments the serial;
retries reuse it. Full IDs are canonical zero-padded 32-byte manifest identities.

Version 2 request: exactly 50 bytes. Version 3 contextual requests add a
track and selector before the ID (52 bytes total); only metadata operations
use this extension. See `pi/engine/dn2_ui_wire.h` for bounds and encoding.

| Bytes | Field |
|---|---|
| 0–5 | `F0 7D 44 32 02 03` |
| 6–9 | Session token |
| 10–13 | Request serial |
| 14–16 | Operation, argument, first |
| 17–48 | Complete engine ID; all zero for catalog requests |
| 49 | `F7` |

Reply: at most 128 bytes, one reply per request.

| Bytes | Field |
|---|---|
| 0–5 | `F0 7D 44 32 02 04` |
| 6–16 | Echoed token, serial, operation, argument, first |
| 17–21 | 32-bit metadata revision in five seven-bit groups; top group ≤15 |
| 22–end−1 | Operation payload |
| end | `F7` |

- Operation 0: descriptor. Argument 0 is the existing `rmp_build_meta_head`
  payload from byte 9, excluding its final F7: state, flags, knob count, part
  count, name[10], short[4], page-two title[13], fingerprint[2]. The fingerprint
  is zero, deliberately not used as identity. The descriptor is padded to sixteen
  positions, so count=16 and parts=5. Arguments 1–4 contain the four corresponding
  24-byte `rmp_build_meta_part` knob records. First is zero. Replies are 56/119 bytes.
- Operation 1: enum names. Argument is knob index 0–15; first is 0/8/16/24/32.
  Payload is total count, returned count, then up to eight canonical name[8]
  strings, each at most seven printable characters. Maximum total is 36. No
  truncation to the older Rytm's sixteen-name limit. Replies are at most 89 bytes.
- Operation 2: installed catalog. Argument is index 0–31, first zero, ID empty.
  Payload is total count, state, ID[32], name[10], short[4]. Out-of-range indices
  return the total and an empty entry. Replies are 71 bytes. Catalog revision
  includes package inventory and slot availability/identity.
- Operation 3: explicit load. Argument is DN2 track 0–15, first zero, ID is the
  full selected engine. The Pi checks that the track belongs to the negotiated
  native session. Payload is one RMP state byte; revision is zero, reply 24 bytes.
  READY reuses an existing ready assignment. LOADING means queued/in progress or
  waiting for the map/scan/loader. The DN2 polls at 100 ms with fresh serials after
  LOADING replies; unanswered retries keep their serial. Other states terminate
  the request. Cancel stops polling but an accepted Pi load may finish into cache.

State uses RMP's EMPTY/READY/LOADING/MISSING/BAD/NO_ROOM/MUTED values. EMPTY for an
installed catalog entry means **installed but not loaded**, not ready to play.
There are five loaded machine slots shared by eight independent parts. No slot
is evicted or assigned by a metadata read.

The explicit loader prefers an empty slot, then reclaims an assigned cache entry
only when no monophonic voice, polyphonic instance or committed part reserves it.
A stopped, muted or not-yet-playing part still reserves its engine. If all five types are referenced,
the request returns NO_ROOM. Native loads cannot trigger global image/arena
compaction: allocation failure remains local. At full capacity the picker offers
SWAP ENGINE only for a complete installed-but-unloaded descriptor (EMPTY) and a
NO_ROOM load reply. A final YES commits its identity/defaults to the DN2 sound.
The resulting committed STATE releases that part's old plugin state and queues
the new full ID; browsing and initial loading never release the current sound.
A shared old engine stays protected by its other parts. If five other engine
types still remain reserved, the new part stays unavailable rather than evicting
one of them. An already-failed assigned package is not offered as a capacity swap.

Committed STATE also queues missing saved engines during recall. Each part holds
one latest ID, serviced round-robin by the existing loader idle loop, at most one
attempt per 25 ms unless new intent arrives. The event receiver does no file I/O.
When loading completes, the newest sixteen controls are applied without replaying
missed note-ons. Plain KILL releases the part; STATE|KILL and RESET only silence
it and retain its reservation. Session teardown releases all participating parts.
Missing packages can recover after a rescan; terminal assigned load failures are
not automatically reloaded. This is event-driven recall, not a complete project
inventory preload before the DN2 has sent any state.

## Ownership and publication

Pi requests are accepted only in an active native session. Session admission no
longer requires a ready plugin: an empty Pi must still expose its catalog and load
controls. USB epochs, ACK/RESET and leases govern the session. Slot lifecycle
generations govern individual receivers; loading an unrelated slot does not reset
the session or cut other engines' tails. Pending replacements are not advertised
as READY merely because the slot still holds the outgoing image.

The control core reads
cached package descriptors/names and queues a complete response. A two-entry SPSC
queue feeds the USB core; leases have priority. The USB core rejects obsolete
transport epochs and session tokens. Ordinary Rytm output remains blocked on DN2.

DN2 keeps two buffers for one current engine, not every installed engine's enums.
The MIDI task validates full replies, expected serial/part, field bounds and
revision before writing staging. A revision change restarts from the head. Only
the UI task publishes a complete descriptor, at the start of a UI pass. Native
record names/titles have their own stable storage, not pointers into MIDI staging.
Capture/audio use the sound's saved ID and sixteen values independently.

Retry interval is 150 native frames (100 ms). Once published, a head refresh is
due every 1,500 frames (one second); an unchanged revision avoids another full
transaction. Unknown/unavailable metadata never replaces saved sound values.

Revision mixes the existing full descriptor fingerprint with package CRC and the
current label-set index, so name-table/set changes invalidate the cache. It is a cache-coherency hash, not a
collision-free identity or cryptographic guarantee. Dynamic RMP label sets still
follow Sidecar's existing slot-global label selection; per-DN2-part dynamic label
selection and external presets/sample-pool browsing are not implemented here.

## Verification

See [bridge preparation](README.md) for focused checks and remaining hardware
qualification. Cross-repository tests use original metadata-only fixtures.
