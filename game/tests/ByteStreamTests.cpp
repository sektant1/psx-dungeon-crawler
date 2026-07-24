#include "ByteStream.h"

#include <cstdlib>
#include <iostream>

using namespace mapio;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ByteStreamTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    ByteWriter w;
    w.u8(0x12);
    w.u16(0x3456);
    w.u32(0x789ABCDEu);
    w.f32(1.5f);
    w.vec3({1.0f, -2.0f, 3.0f});
    w.quat(glm::quat(0.0f, 0.0f, 1.0f, 0.0f)); // w,x,y,z
    w.str("floor");
    w.str("wall");
    w.str("floor"); // deduped -> same pool index

    require(w.pool().size() == 2, "string pool dedups repeats");
    require(w.pool()[0] == "floor" && w.pool()[1] == "wall", "pool order preserved");

    ByteReader r(w.bytes().data(), w.bytes().size(), w.pool());
    require(r.u8() == 0x12, "u8 round-trips");
    require(r.u16() == 0x3456, "u16 round-trips");
    require(r.u32() == 0x789ABCDEu, "u32 round-trips");
    require(r.f32() == 1.5f, "f32 round-trips");
    const glm::vec3 v = r.vec3();
    require(v.x == 1.0f && v.y == -2.0f && v.z == 3.0f, "vec3 round-trips");
    const glm::quat q = r.quat();
    require(q.w == 0.0f && q.x == 0.0f && q.y == 1.0f && q.z == 0.0f, "quat round-trips");
    require(r.str() == "floor", "first string round-trips");
    require(r.str() == "wall", "second string round-trips");
    require(r.str() == "floor", "deduped string resolves to same value");
    require(r.ok(), "reader stayed in bounds");
    require(r.remaining() == 0, "reader consumed everything");

    ByteReader over(w.bytes().data(), 1, w.pool());
    over.u32();
    require(!over.ok(), "reading past the end flags an error");

    std::cout << "ByteStreamTests OK\n";
    return 0;
}
