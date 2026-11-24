# Add a Digitone II host profile with eight native parts

Draft PR body for `trigcondition/sidecar-firmware:dev`. Not posted. The maintainer's
issue/claim workflow still applies; no issue has been claimed for this proposal.
The companion DN2MK2 source contribution must be linked when submitting.

## What and why

Add an opt-in DN2 host profile so a modified Digitone II can select Sidecar
engines by name, edit up to sixteen fractional controls on native SYN pages,
and sequence eight independent parts. Dry mono audio returns to each assigned
DN2 voice before its filter, AMP, pan and FX sends.

The adapter supports existing Sidecar built-ins and compatible RMP plugins. The
public SC Snare plugin provides a demo. No external drum implementations or
vendor firmware are included. Adding compatible plugins requires no per-engine
DN2 patch. The related DN2 endpoint targets DN2MK2 0.72 / Elektron OS 1.11.

The loader releases retired executable/writable image pages and permits eight
resident images on Pi builds. Five-slot targets retain their existing map size.
The DN2 host reserves active parts by full engine identity; stopped/muted parts
keep their reservations, and unrelated parts are not evicted by a selection.

## How it was tested

Commands and current evidence are in [README](README.md). Focused checks cover
the codecs, eight-part ownership, reconnect, metadata, deferred loading and the
actual USB driver with Circle shims. The allocator fixtures exercise both five
and eight slots. Compiled DN2 packets and public SC Snare metadata provide
cross-repository interoperability evidence.

Local Linux results are phased: native portable tests passed with thirteen
documented dependency skips; web tests have 101/102 passes plus an isolated pass
for an intermittent pairing case (also checked against unmodified upstream).
The DN2 ASan/UBSan phase and both allocator configurations pass. Full upstream CI
has not run, and the local whole-script invocation was not all green. Details,
including the Windows-checkout newline issue, are in the review guide.

The new host checks are included in the existing portable-test entry point.
No device was updated during preparation. The macOS core CI job, clean Pi kernel
build and hardware qualification remain required before calling this a release;
host tests do not establish worst-case audio performance or physical latency.

## Protocol, settings and compatibility

- `dn2=1` opts into the DN2 profile. Its native session requests tracks 1–8;
  `dn2auto=0` retains manual/stock-MIDI startup. Existing Rytm profile defaults
  remain selected without DN2 configuration.
- [Event/session protocol](PROTOCOL.md) and [metadata protocol](UI-PROTOCOL.md)
  document the bounded messages and persistent IDs. This is a distinct DN2
  namespace; ordinary Rytm traffic is kept out of the DN2 parser and vice versa.
- ACK then RESET is required before native ownership. An unmodified DN2 or an
  older Pi lacking this endpoint cannot provide native parts; it does not gain
  ownership from an unknown packet. Full general capability negotiation is
  follow-up work.
- No intentional sound/default/range change for existing Rytm kits or plugins.
  USB driver tests include the existing Rytm/mirror paths; live Rytm regression
  and boot-order checks are not claimed for this integrated branch.

## Checklist

- [ ] Required upstream `test` and macOS `core` CI checks pass on the PR.
- [x] No Elektron firmware, captured sections, dumps or card images are included.
- [x] No device credentials, home paths or private engine library are included.
- [x] Existing third-party licence notices are preserved; no new engine is vendored.

The loader and host commits are separate review slices. The DN2 companion is a
functional dependency, not a requirement to merge the two repositories together.
