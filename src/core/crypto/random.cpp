#include "seventhslide/crypto/random.hpp"

#include "openssl_support.hpp"

#include <openssl/rand.h>

namespace seventhslide::crypto {

std::string random_bytes(std::size_t n)
{
    std::string out(n, '\0');
    if (n > 0) {
        openssl::check(RAND_bytes(reinterpret_cast<unsigned char*>(out.data()),
                                  static_cast<int>(n)),
                       "No random bytes could be had from the system");
    }
    return out;
}

}  // namespace seventhslide::crypto
