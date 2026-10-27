#pragma once

#include <vector>
#include <span>
#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>

namespace titanhub {

class CompressionEngine {
public:
    static bool IsAllZero(std::span<const uint8_t> src) noexcept {
        const size_t u64Count = src.size() / sizeof(uint64_t);
        const auto* u64Ptr = reinterpret_cast<const uint64_t*>(src.data());
        for (size_t i = 0; i < u64Count; ++i) {
            if (u64Ptr[i] != 0) return false;
        }
        for (size_t i = u64Count * sizeof(uint64_t); i < src.size(); ++i) {
            if (src[i] != 0) return false;
        }
        return true;
    }

    static size_t CompressRLE(std::span<const uint8_t> src, std::vector<uint8_t>& dst) {
        dst.clear();
        if (src.empty()) return 0;

        if (IsAllZero(src)) {
            dst.push_back(0xFF);
            const uint64_t sz = src.size();
            for (size_t i = 0; i < 8; ++i) {
                dst.push_back(static_cast<uint8_t>((sz >> (i * 8)) & 0xFF));
            }
            return dst.size();
        }

        size_t i = 0;
        while (i < src.size()) {
            uint8_t val = src[i];
            size_t run = 1;
            while (i + run < src.size() && src[i + run] == val && run < 255) {
                ++run;
            }
            dst.push_back(static_cast<uint8_t>(run));
            dst.push_back(val);
            i += run;
        }
        return dst.size();
    }

    static bool DecompressRLE(std::span<const uint8_t> src, std::span<uint8_t> dst) noexcept {
        if (src.empty()) return true;
        if (src[0] == 0xFF && src.size() == 9) {
            std::memset(dst.data(), 0, dst.size());
            return true;
        }

        size_t srcIdx = 0;
        size_t dstIdx = 0;
        while (srcIdx + 1 < src.size() && dstIdx < dst.size()) {
            uint8_t run = src[srcIdx++];
            uint8_t val = src[srcIdx++];
            size_t toWrite = std::min(static_cast<size_t>(run), dst.size() - dstIdx);
            std::memset(dst.data() + dstIdx, val, toWrite);
            dstIdx += toWrite;
        }
        return dstIdx == dst.size();
    }

    static size_t CompressLZ4(std::span<const uint8_t> src, std::vector<uint8_t>& dst) {
        dst.clear();
        const size_t srcSize = src.size();
        if (srcSize == 0) return 0;

        dst.reserve(srcSize + (srcSize / 255) + 16);

        constexpr size_t HASH_BITS = 12;
        constexpr size_t HASH_SIZE = 1 << HASH_BITS;
        std::array<int32_t, HASH_SIZE> hashTable{};
        hashTable.fill(-1);

        auto hash4 = [](const uint8_t* p) noexcept -> uint32_t {
            uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return (v * 2654435761U) >> (32 - HASH_BITS);
        };

        const uint8_t* const srcBase = src.data();
        const uint8_t* ip = srcBase;
        const uint8_t* const ipEnd = srcBase + srcSize;
        const uint8_t* const mflimit = ipEnd - 12;
        const uint8_t* anchor = ip;

        while (ip < mflimit) {
            uint32_t h = hash4(ip);
            int32_t matchPos = hashTable[h];
            hashTable[h] = static_cast<int32_t>(ip - srcBase);

            if (matchPos >= 0 && (ip - (srcBase + matchPos)) < 65535 &&
                std::memcmp(ip, srcBase + matchPos, 4) == 0) {
                
                const uint8_t* match = srcBase + matchPos;
                size_t litLen = ip - anchor;
                size_t matchLen = 4;
                while (ip + matchLen < ipEnd && ip[matchLen] == match[matchLen]) {
                    ++matchLen;
                }

                uint16_t offset = static_cast<uint16_t>(ip - match);

                uint8_t token = static_cast<uint8_t>((std::min(litLen, size_t(15)) << 4) |
                                                     (std::min(matchLen - 4, size_t(15))));
                dst.push_back(token);

                if (litLen >= 15) {
                    size_t rem = litLen - 15;
                    while (rem >= 255) {
                        dst.push_back(255);
                        rem -= 255;
                    }
                    dst.push_back(static_cast<uint8_t>(rem));
                }

                dst.insert(dst.end(), anchor, anchor + litLen);

                dst.push_back(static_cast<uint8_t>(offset & 0xFF));
                dst.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));

                if (matchLen - 4 >= 15) {
                    size_t rem = (matchLen - 4) - 15;
                    while (rem >= 255) {
                        dst.push_back(255);
                        rem -= 255;
                    }
                    dst.push_back(static_cast<uint8_t>(rem));
                }

                ip += matchLen;
                anchor = ip;
            } else {
                ip += 1;
            }
        }

        size_t litLen = ipEnd - anchor;
        if (litLen > 0) {
            uint8_t token = static_cast<uint8_t>(std::min(litLen, size_t(15)) << 4);
            dst.push_back(token);
            if (litLen >= 15) {
                size_t rem = litLen - 15;
                while (rem >= 255) {
                    dst.push_back(255);
                    rem -= 255;
                }
                dst.push_back(static_cast<uint8_t>(rem));
            }
            dst.insert(dst.end(), anchor, anchor + litLen);
        }

        return dst.size();
    }

    static bool DecompressLZ4(std::span<const uint8_t> src, std::span<uint8_t> dst) noexcept {
        const uint8_t* ip = src.data();
        const uint8_t* const ipEnd = ip + src.size();
        uint8_t* op = dst.data();
        uint8_t* const opEnd = op + dst.size();

        while (ip < ipEnd) {
            uint8_t token = *ip++;
            size_t litLen = (token >> 4) & 0x0F;
            if (litLen == 15) {
                uint8_t s;
                do {
                    if (ip >= ipEnd) return false;
                    s = *ip++;
                    litLen += s;
                } while (s == 255);
            }

            if (op + litLen > opEnd || ip + litLen > ipEnd) return false;
            std::memcpy(op, ip, litLen);
            op += litLen;
            ip += litLen;

            if (ip >= ipEnd) break;

            if (ip + 2 > ipEnd) return false;
            uint16_t offset = static_cast<uint16_t>(ip[0] | (ip[1] << 8));
            ip += 2;
            if (offset == 0) return false;

            size_t matchLen = (token & 0x0F) + 4;
            if (matchLen == 19) {
                uint8_t s;
                do {
                    if (ip >= ipEnd) return false;
                    s = *ip++;
                    matchLen += s;
                } while (s == 255);
            }

            if (op + matchLen > opEnd) return false;
            const uint8_t* match = op - offset;
            if (match < dst.data()) return false;

            for (size_t i = 0; i < matchLen; ++i) {
                op[i] = match[i];
            }
            op += matchLen;
        }

        return op == opEnd;
    }
};

} // namespace titanhub
