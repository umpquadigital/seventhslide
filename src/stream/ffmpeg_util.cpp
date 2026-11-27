#include "ffmpeg_util.hpp"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/log.h>
}

#include <mutex>

namespace seventhslide::stream {

void quiet_ffmpeg()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (av_log_get_level() > AV_LOG_ERROR) {
            av_log_set_level(AV_LOG_ERROR);
        }
    });
}

std::string av_error_text(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

}  // namespace seventhslide::stream
