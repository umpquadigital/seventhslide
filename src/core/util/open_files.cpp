#include "seventhslide/util/open_files.hpp"

#if defined(__APPLE__)
#include <sys/resource.h>

#include <algorithm>
#endif

namespace seventhslide::util {

void raise_open_file_limit() noexcept
{
#if defined(__APPLE__)
    // setrlimit refuses a soft limit above OPEN_MAX, even when the hard limit
    // is RLIM_INFINITY (as it is by default).
    constexpr rlim_t k_most = 10240;
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        return;
    }
    const rlim_t wanted = std::min(limit.rlim_max, k_most);
    if (limit.rlim_cur >= wanted) {
        return;
    }
    limit.rlim_cur = wanted;
    (void)setrlimit(RLIMIT_NOFILE, &limit);
#endif
}

}  // namespace seventhslide::util
