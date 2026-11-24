# Sharing a personal-use Digitone II host integration for possible adoption

Feature discussion for the Sidecar contribution workflow; not an offer to take
on ongoing maintenance.

I've developed an AI-assisted, personal-use Digitone II host integration, paired
with a DN2MK2 endpoint, and would like to make the generic work available for
possible adoption. It exposes Sidecar built-ins and compatible RMP plugins in
native machine selection, with up to sixteen controls, parameter locks and
eight mono returns through the DN2's own filter/AMP/pan/FX sends.

The integration is prepared on `dev`. Loader memory reclamation/eight resident
images and the DN2 adapter are separate commits. The public SC Snare plugin
demonstrates the interface; no private engines or vendor firmware are included.
The event/session/metadata contracts and portable tests accompany the code.

Existing Rytm mode remains the default. DN2 mode is explicit and needs a matching
session before taking ownership. Hardware timing and a release-qualified build
are not claimed for this rebased contribution.

The source is available on the [experimental Pi branch](https://github.com/Ericxgao/sidecar-firmware/tree/propose/dn2-native).
The [DN2MK2 companion](https://github.com/Ericxgao/dn2mk2-firmware/tree/propose/sidecar-native)
is being submitted separately for upstream inclusion. Users currently need both
modified codebases; an upstream Sidecar release alone does not provide this.

I'm not in a position to independently maintain a supported DN2 edition, track
every upstream release, or provide ongoing user support. I'm sharing the work
and its evidence, not asking to be assigned as its long-term maintainer. For my
own use I can keep a known working pair of versions pinned.

Would this fit Sidecar's direction, and would there be interest in adopting the
host profile or parts of the loader work? If not, I'm happy for this to remain
an explicitly experimental external branch rather than imply official support.

Validation: native portable tests passed with thirteen dependency skips; the
DN2 phase passed with ASan/UBSan and both allocator maps passed 20,000 swaps.
Web results were phased, with an intermittent pairing test passing separately.
Full green CI, macOS core tests, a new Pi kernel build and hardware qualification
are still outstanding. The [review guide](https://github.com/Ericxgao/sidecar-firmware/blob/propose/dn2-native/docs/dn2/README.md)
and [validation receipt](https://github.com/Ericxgao/sidecar-firmware/blob/propose/dn2-native/docs/dn2/validation.json)
record the exact scope. No binary release or private plugin library is included.
