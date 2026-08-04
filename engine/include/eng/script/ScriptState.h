#pragma once
#include <cstdint>
#include <vector>

namespace eng::script {

// The live script instances for this entity's Scripts, owned by ScriptHost.
// Written by the host, never by an author or a caller -- the same contract as
// NodeRef and BodyRef, and writing one by hand strands an instance the same way
// writing a BodyRef strands a body.
//
// Holds opaque pool slots rather than sol types, so this header does not drag
// the VM into anything that includes it. Deliberately NOT registered with the
// ComponentRegistry: it is never serialised and never authored.
struct ScriptState {
    std::vector<uint32_t> instances; // slots in ScriptHost's instance pool
};

} // namespace eng::script
