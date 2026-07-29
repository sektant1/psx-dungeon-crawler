#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng::io {

// Little-endian primitive writer with an interned, order-preserving string
// pool. Strings are written as a u32 pool index; the pool itself is emitted
// separately (by MapSerializer) so component payloads stay compact.
class ByteWriter {
public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void f32(float v);
    void vec3(const glm::vec3& v);
    void quat(const glm::quat& q);
    void str(const std::string& s); // interns, writes u32 index

    uint32_t intern(const std::string& s); // pool index, appends if new
    // Overwrite 4 bytes previously written at byte offset `at` (little-endian).
    // Used to backfill a length prefix once the payload size is known.
    void patchU32(std::size_t at, uint32_t v);

    const std::vector<uint8_t>& bytes() const { return mBuf; }
    const std::vector<std::string>& pool() const { return mPool; }
    std::size_t size() const { return mBuf.size(); }

private:
    std::vector<uint8_t> mBuf;
    std::vector<std::string> mPool;
};

// Little-endian primitive reader. Never reads out of bounds: once an overrun
// is attempted, ok() returns false and further reads yield zero/defaults.
// Borrows the string pool by const reference: the pool MUST outlive the reader.
class ByteReader {
public:
    ByteReader(const uint8_t* data, std::size_t len,
               const std::vector<std::string>& pool);

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    float f32();
    glm::vec3 vec3();
    glm::quat quat();
    const std::string& str(); // reads u32 index into the pool

    void skip(std::size_t n);
    std::size_t remaining() const { return mOk ? std::size_t(mEnd - mCur) : 0; }
    bool ok() const { return mOk; }

private:
    bool take(std::size_t n); // bounds-check n bytes, clear mOk on overrun
    const uint8_t* mCur;
    const uint8_t* mEnd;
    const std::vector<std::string>& mPool;
    bool mOk = true;
    std::string mEmpty;
};

} // namespace eng::io
