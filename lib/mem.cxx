#include "internal.h"
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace jitlib
{
    namespace native
    {
        uint8_t *allocate(std::size_t &size)
        {
            long pagesize = sysconf(_SC_PAGE_SIZE);
            ASSERT(pagesize > 0);
            size = ((size - 1) | (pagesize - 1)) + 1;
            auto *const code = (uint8_t *)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            ASSERT(code != MAP_FAILED);
            return code;
        }

        void finalise(uint8_t *buffer, std::size_t used, std::size_t length)
        {
            // Trap on any leftover space
            uint8_t *unused_start = buffer + used;
            std::size_t unused_length = length - used;
#ifdef __arm__
            const uint32_t udf = 0xe7f000f0;
            std::fill_n(reinterpret_cast<uint32_t*>(unused_start), unused_length / 4, udf);
#elif defined(__x86_64__) || defined(__i386__)
            const uint8_t int3 = 0xcc;
            memset(unused_start, int3, unused_length);
#else
#error "Unknown platform"
#endif

            // Make it executable
            int err = mprotect(buffer, length, PROT_READ | PROT_EXEC);
            ASSERT(err == 0);
        }

        void deallocate(void *buffer, std::size_t length)
        {
            munmap(buffer, length);
        }
    }
}
