#pragma once

// How many computers heard on the network ``Discovery`` and ``Reach`` keep
// track of.

#include <cstddef>

namespace seventhslide::link {

/// Far more than one network has: anyone there may announce any number of
/// keys. The one heard longest ago goes first; ``Reach`` never lets a member,
/// or a computer connected, go for one.
inline constexpr std::size_t k_most_heard = 256;

}  // namespace seventhslide::link
