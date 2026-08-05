#include <eng/io/ByteStream.h>

#include <cstring>

namespace eng::io {

void ByteWriter::u8(uint8_t v) { mBuf.push_back(v); }

void ByteWriter::u16(uint16_t v)
{
    mBuf.push_back(uint8_t(v & 0xFF));
    mBuf.push_back(uint8_t((v >> 8) & 0xFF));
}

void ByteWriter::u32(uint32_t v)
{
    for (int i = 0; i < 4; ++i) mBuf.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}

void ByteWriter::u64(uint64_t v)
{
    for (int i = 0; i < 8; ++i) mBuf.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}

void ByteWriter::f32(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(bits);
}

void ByteWriter::vec3(const glm::vec3& v) { f32(v.x); f32(v.y); f32(v.z); }

void ByteWriter::quat(const glm::quat& q) { f32(q.w); f32(q.x); f32(q.y); f32(q.z); }

uint32_t ByteWriter::intern(const std::string& s)
{
    for (uint32_t i = 0; i < mPool.size(); ++i)
        if (mPool[i] == s) return i;
    mPool.push_back(s);
    return uint32_t(mPool.size() - 1);
}

void ByteWriter::str(const std::string& s) { u32(intern(s)); }

void ByteWriter::patchU32(std::size_t at, uint32_t v)
{
    if (at + 4 > mBuf.size()) return; // out-of-range patch is a no-op
    for (int i = 0; i < 4; ++i) mBuf[at + std::size_t(i)] = uint8_t((v >> (8 * i)) & 0xFF);
}

void ByteWriter::raw(const void* data, std::size_t n)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    mBuf.insert(mBuf.end(), bytes, bytes + n);
}

bool ByteReader::raw(void* out, std::size_t n)
{
    if (!take(n))
        return false;
    std::memcpy(out, mCur - n, n);
    return true;
}

ByteReader::ByteReader(const uint8_t* data, std::size_t len,
                       const std::vector<std::string>& pool)
    : mCur(data), mEnd(data + len), mPool(pool)
{}

bool ByteReader::take(std::size_t n)
{
    if (!mOk || std::size_t(mEnd - mCur) < n) { mOk = false; return false; }
    return true;
}

uint8_t ByteReader::u8()
{
    if (!take(1)) return 0;
    return *mCur++;
}

uint16_t ByteReader::u16()
{
    if (!take(2)) return 0;
    uint16_t v = uint16_t(mCur[0]) | (uint16_t(mCur[1]) << 8);
    mCur += 2;
    return v;
}

uint32_t ByteReader::u32()
{
    if (!take(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(mCur[i]) << (8 * i);
    mCur += 4;
    return v;
}

uint64_t ByteReader::u64()
{
    if (!take(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(mCur[i]) << (8 * i);
    mCur += 8;
    return v;
}

float ByteReader::f32()
{
    uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

glm::vec3 ByteReader::vec3()
{
    float x = f32(), y = f32(), z = f32();
    return {x, y, z};
}

glm::quat ByteReader::quat()
{
    float w = f32(), x = f32(), y = f32(), z = f32();
    return glm::quat(w, x, y, z);
}

const std::string& ByteReader::str()
{
    uint32_t idx = u32();
    if (!mOk || idx >= mPool.size()) { mOk = false; return mEmpty; }
    return mPool[idx];
}

std::optional<ByteReader> ByteReader::slice(std::size_t n)
{
    if (!take(n)) return std::nullopt;
    ByteReader result(mCur, n, mPool);
    mCur += n;
    return result;
}

void ByteReader::skip(std::size_t n)
{
    if (take(n)) mCur += n;
}

} // namespace eng::io
