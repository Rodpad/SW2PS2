// Check the production MFIFO invalidation ranges against ring writes.
#include "../third_party/ouncesx2-eval/src/pcsx2/MfifoWriteRange.h"
#include <cassert>
#include <vector>
#include <cstdio>

int main()
{
    for (unsigned ring = 1; ring <= 256; ++ring)
        for (unsigned start = 0; start < ring; ++start)
            for (unsigned count = 0; count <= ring + 2; ++count) {
                const auto r = GetMfifoWriteRanges(start, ring, count);
                assert(r.first_start <= ring && r.first_count <= ring - r.first_start);
                assert(r.second_count <= ring);
                std::vector<bool> covered(ring);
                for (unsigned i = 0; i < r.first_count; ++i) covered[r.first_start + i] = true;
                for (unsigned i = 0; i < r.second_count; ++i) covered[i] = true;
                for (unsigned i = 0; i < count; ++i) assert(covered[(start + i) % ring]);
                if (count < ring) {
                    unsigned n = 0;
                    for (bool b : covered) n += b;
                    assert(n == count);
                }
            }
    std::puts("MFIFO write ranges cover all written slots, including wrapped and oversized transfers.");
}
