#include <catch2/catch_test_macros.hpp>

#include <seventhslide/remote/remote_auth.hpp>

#include <chrono>
#include <thread>

using seventhslide::remote::Auth;

TEST_CASE("Auth: an empty PIN means open access", "[remote][R4]")
{
    Auth auth;
    REQUIRE(auth.open_access());
    // The settings dialog promises "leave blank for no authentication".
    REQUIRE(auth.check_pin(QString{}));
    REQUIRE(auth.check_pin(QStringLiteral("anything")));
}

TEST_CASE("Auth: a set PIN admits only itself", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("4321"));
    REQUIRE_FALSE(auth.open_access());
    REQUIRE(auth.check_pin(QStringLiteral("4321")));
    REQUIRE_FALSE(auth.check_pin(QStringLiteral("4320")));
    REQUIRE_FALSE(auth.check_pin(QStringLiteral("43210")));
    REQUIRE_FALSE(auth.check_pin(QString{}));
}

TEST_CASE("Auth: tokens are minted, honoured, and revocable", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("1234"));

    const QString token = auth.mint_token();
    REQUIRE_FALSE(token.isEmpty());
    REQUIRE(auth.valid_token(token));
    REQUIRE_FALSE(auth.valid_token(QStringLiteral("not-a-token")));
    REQUIRE_FALSE(auth.valid_token(QString{}));

    auth.clear_tokens();
    REQUIRE_FALSE(auth.valid_token(token));
}

TEST_CASE("Auth: changing the PIN invalidates issued tokens", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("1111"));
    const QString token = auth.mint_token();
    REQUIRE(auth.valid_token(token));

    // A device admitted under the old PIN must not survive the change.
    auth.set_pin(QStringLiteral("2222"));
    REQUIRE_FALSE(auth.valid_token(token));

    // Clearing the PIN is also a change, and also revokes.
    const QString second = auth.mint_token();
    auth.set_pin(QString{});
    REQUIRE_FALSE(auth.valid_token(second));
}

TEST_CASE("Auth: repeated failures put a peer into backoff", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("9999"));
    const QString peer = QStringLiteral("192.168.1.50");

    REQUIRE(auth.allow_attempt(peer));
    for (int i = 0; i < Auth::k_free_attempts; ++i) {
        auth.note_failure(peer);
    }
    // Still inside the free allowance.
    REQUIRE(auth.allow_attempt(peer));
    REQUIRE(auth.lockout_remaining(peer) == 0);

    auth.note_failure(peer);
    auth.note_failure(peer);
    REQUIRE_FALSE(auth.allow_attempt(peer));
    REQUIRE(auth.lockout_remaining(peer) > 0);
    REQUIRE(auth.lockout_remaining(peer) <= Auth::k_max_lockout_seconds);

    // A correct PIN clears the record so the operator is not left locked out.
    auth.note_success(peer);
    REQUIRE(auth.allow_attempt(peer));
    REQUIRE(auth.lockout_remaining(peer) == 0);
}

TEST_CASE("Auth: the first lockout holds for its whole second", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("9999"));
    const QString peer = QStringLiteral("192.168.1.51");
    for (int i = 0; i < Auth::k_free_attempts + 1; ++i) {
        auth.note_failure(peer);
    }
    // One second, with a tenth of it gone (as when the next guess arrives):
    // still locked, and the client is told to wait one second, not zero.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE_FALSE(auth.allow_attempt(peer));
    REQUIRE(auth.lockout_remaining(peer) == 1);
}

TEST_CASE("Auth: backoff is tracked per peer", "[remote][R4]")
{
    Auth auth;
    auth.set_pin(QStringLiteral("9999"));
    const QString attacker = QStringLiteral("192.168.1.99");
    const QString operator_phone = QStringLiteral("192.168.1.20");

    for (int i = 0; i < Auth::k_free_attempts + 3; ++i) {
        auth.note_failure(attacker);
    }
    REQUIRE_FALSE(auth.allow_attempt(attacker));
    REQUIRE(auth.allow_attempt(operator_phone));
}
