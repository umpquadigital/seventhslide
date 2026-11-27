#pragma once

// Auth - PIN gate, bearer tokens, per-peer rate limiting.
//
// The settings dialog promises "Leave blank for no authentication. Use a PIN on
// untrusted networks." This honours that exactly: an empty PIN means open
// access on the LAN, a set PIN gates both the web app and the WebSocket.
//
// This is plain HTTP. The PIN controls access; it does not encrypt anything.

#include <QDateTime>
#include <QHash>
#include <QString>

#include <chrono>
#include <optional>

namespace seventhslide::remote {

class Auth {
public:
    /// How long an issued token stays valid, unless set_token_ttl says.
    static constexpr std::chrono::seconds k_token_ttl{12 * 60 * 60};
    /// Failures tolerated from one peer before backoff starts.
    static constexpr int k_free_attempts = 5;
    /// Upper bound on the backoff, so a locked-out operator recovers.
    static constexpr qint64 k_max_lockout_seconds = 15 * 60;

    /// Replace the PIN. Any change - including clearing it - invalidates every
    /// token already issued.
    void set_pin(const QString& pin);
    [[nodiscard]] bool open_access() const noexcept { return pin_.isEmpty(); }

    /// Constant-time PIN comparison. Always compares the full length so a
    /// timing observer learns nothing about the prefix.
    [[nodiscard]] bool check_pin(const QString& candidate) const;

    /// How long tokens minted from now on stay valid.
    void set_token_ttl(std::chrono::seconds ttl) noexcept { ttl_ = ttl; }

    [[nodiscard]] QString mint_token();
    /// True when the token is known and unexpired. Sweeps expired entries.
    [[nodiscard]] bool valid_token(const QString& token);
    /// When ``token`` stops being valid; nullopt for a token never issued
    /// (or already dropped).
    [[nodiscard]] std::optional<QDateTime> token_expiry(const QString& token) const;
    void clear_tokens();

    /// False when this peer is in backoff and must not be allowed to guess.
    [[nodiscard]] bool allow_attempt(const QString& peer) const;
    void note_failure(const QString& peer);
    void note_success(const QString& peer);
    /// Seconds remaining before this peer may try again, rounded up; 0 when
    /// not locked out.
    [[nodiscard]] qint64 lockout_remaining(const QString& peer) const;

private:
    struct Attempts {
        int failures = 0;
        QDateTime locked_until;
    };

    QString pin_;
    std::chrono::seconds ttl_ = k_token_ttl;
    QHash<QString, QDateTime> tokens_;  ///< token -> expiry
    QHash<QString, Attempts> attempts_; ///< peer address -> backoff state
};

} // namespace seventhslide::remote
