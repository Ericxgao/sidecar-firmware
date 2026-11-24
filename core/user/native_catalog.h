// Read-only installed-machine metadata; control core, cached data only.
// Full IDs are persistent identity. Slot numbers, revisions and list indices are not.
#pragma once
#include "rmp_manifest.h"
namespace um {
struct NativeDescriptor {
    rmp_desc desc{};
    uint32_t revision = 0;
    uint8_t state = 0, flags = 0;
};
struct NativeCatalogEntry {
    char id[32]{}, name[11]{}, short_name[5]{};
    uint32_t revision = 0;
    uint8_t count = 0, state = 0;
};
// Unknown IDs get a missing descriptor, not a fallback engine. No assignment/load.
void native_descriptor(const char* id, NativeDescriptor& out);
// Index may exceed count (including the empty catalog); zero entry then reports count.
void native_catalog(unsigned index, NativeCatalogEntry& out);
int native_names(const char* id, unsigned p1, char names[][8]);
// Explicit control request, not a metadata read. Reuse a matching assignment or
// queue an unreferenced slot through the existing asynchronous loader. Prefer
// empty entries, then reclaim cached assignments with no voice/instance owners.
// Never replace a referenced engine or compact every image. Returns RMP_PST_*.
uint8_t native_load(const char* id);
// Control-event path: bounded intent/reservation only. idle() services loading.
bool native_request(unsigned part, const char* id);
void native_forget(unsigned part);
bool native_poll(uint32_t now); // one fair, rate-limited loader step; called by idle()
} // namespace um
