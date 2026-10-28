#ifndef POLLEN_MEDIA_WINDOWS_HPP
#define POLLEN_MEDIA_WINDOWS_HPP

#include "pollen/media.hpp"

namespace pollen {

#if defined(_WIN32)
NowPlaying windows_now_playing();
#endif // _WIN32

} // namespace pollen

#endif // POLLEN_MEDIA_WINDOWS_HPP
