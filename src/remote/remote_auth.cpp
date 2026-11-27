#include <seventhslide/remote/remote_auth.hpp>

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <algorithm>

namespace seventhslide::remote {

namespace {

/// SHA-256 of the input. Comparing digests rather than the raw strings makes
/// the comparison independent of both the content and the length of the PIN.
QByteArray digest(const QString& value)
{
    return QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256);
}

/// Byte comparison that always visits every byte.
bool constant_time_equal(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

} // namespace

void Auth::set_pin(const QString& pin)
{
    const QString next = pin.trimmed();
    if (next == pin_) {
        return;
    }
    pin_ = next;
    // A PIN change must not leave previously-admitted devices connected.
    clear_tokens();
}

bool Auth::check_pin(const QString& candidate) const
{
    if (open_access()) {
        return true;
    }
    return constant_time_equal(digest(candidate), digest(pin_));
}

QString Auth::mint_token()
{
    QByteArray raw(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()),
                                          raw.size() / sizeof(quint32));
    const QString token = QString::fromLatin1(raw.toHex());
    tokens_.insert(token, QDateTime::currentDateTimeUtc().addSecs(ttl_.count()));
    return token;
}

std::optional<QDateTime> Auth::token_expiry(const QString& token) const
{
    const auto it = tokens_.constFind(token);
    if (it == tokens_.cend()) {
        return std::nullopt;
    }
    return it.value();
}

bool Auth::valid_token(const QString& token)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (it.value() <= now) {
            it = tokens_.erase(it);
        } else {
            ++it;
        }
    }
    if (token.isEmpty()) {
        return false;
    }
    const auto it = tokens_.constFind(token);
    return it != tokens_.cend() && it.value() > now;
}

void Auth::clear_tokens()
{
    tokens_.clear();
}

bool Auth::allow_attempt(const QString& peer) const
{
    return lockout_remaining(peer) == 0;
}

qint64 Auth::lockout_remaining(const QString& peer) const
{
    const auto it = attempts_.constFind(peer);
    if (it == attempts_.cend() || !it->locked_until.isValid()) {
        return 0;
    }
    // Whole seconds, rounded up: a lockout with part of a second left still
    // holds, and still tells the client to wait.
    const qint64 left_ms = QDateTime::currentDateTimeUtc().msecsTo(it->locked_until);
    return left_ms > 0 ? (left_ms + 999) / 1000 : 0;
}

void Auth::note_failure(const QString& peer)
{
    Attempts& a = attempts_[peer];
    ++a.failures;
    if (a.failures <= k_free_attempts) {
        return;
    }
    // Doubling backoff once the free attempts are spent. A four-digit PIN is
    // trivially brute-forceable without this.
    const int over = a.failures - k_free_attempts;
    qint64 seconds = 1;
    for (int i = 1; i < over && seconds < k_max_lockout_seconds; ++i) {
        seconds *= 2;
    }
    a.locked_until = QDateTime::currentDateTimeUtc().addSecs(
        std::min(seconds, k_max_lockout_seconds));
}

void Auth::note_success(const QString& peer)
{
    attempts_.remove(peer);
}

} // namespace seventhslide::remote
