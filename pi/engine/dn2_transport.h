// Typed session requests only. Epoch tags prevent a queued old connection's
// request from being sent to a newly enumerated DN2. No engine/UI reports here.
#pragma once
#include "dn2_link.h"
namespace dn2_native {
struct LinkRequest { LinkControl control; uint32_t epoch; };
inline bool encode_request(const LinkRequest& request, uint32_t epoch, uint8_t* bytes, size_t capacity) {
    return request.epoch == epoch && request.control.command != LinkCommand::Ack &&
           encode_link(request.control, bytes, capacity);
}
}
