# Proposal: Digitone II host support with native controls and eight audio returns

Unsent issue draft for the Sidecar contribution workflow.

I'd like to contribute an opt-in Digitone II host profile, paired with a
DN2MK2 endpoint. It exposes Sidecar built-ins and compatible RMP plugins in
native machine selection, with up to sixteen controls, parameter locks and
eight mono returns through the DN2's own filter/AMP/pan/FX sends.

The integration is prepared on `dev`. Loader memory reclamation/eight resident
images and the DN2 adapter are separate commits. The public SC Snare plugin
demonstrates the interface; no private engines or vendor firmware are included.
The event/session/metadata contracts and portable tests accompany the code.

Existing Rytm mode remains the default. DN2 mode is explicit and needs a matching
session before taking ownership. Hardware timing and a release-qualified build
are not claimed for this rebased contribution.

Would this fit the host-profile direction, and may I take the issue? I can
submit the loader and DN2 integration together or as dependent review slices,
with a linked DN2MK2 proposal. The intended outcome is normal upstream releases
on both sides, while compatible engine packages remain independently installed.
