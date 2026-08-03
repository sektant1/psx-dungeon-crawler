#version 450
// Deliberately empty. The shadow pass has no colour attachments -- the depth
// buffer is its entire product -- but the RHI's pipeline contract requires a
// fragment stage, so this is the smallest thing that satisfies it. Writing no
// outputs means the hardware still gets an early-Z-only pass.
void main() {}
