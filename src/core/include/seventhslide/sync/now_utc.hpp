#pragma once

// The time sync writes into what it keeps and sends, and whether one it
// holds has passed.

#include <QDateTime>

#include <string>
#include <string_view>

namespace seventhslide::sync {

/// Now, in UTC to the second (``2026-11-23T06:00:18Z``). Times in this form
/// compare as text, as expiry times are.
[[nodiscard]] inline std::string now_utc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

/// Whether ``expires``, a time in that form, has passed by this computer's
/// clock.
[[nodiscard]] inline bool expired(std::string_view expires)
{
    return now_utc() > expires;
}

}  // namespace seventhslide::sync
