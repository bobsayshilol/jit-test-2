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
            // INT3 out any leftover space
            memset(buffer + used, 0xcc, length - used);

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
