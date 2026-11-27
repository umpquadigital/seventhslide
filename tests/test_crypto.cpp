// The crypto module: encodings, signing keys and the passphrase-locked box.
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/crypto/encoding.hpp>
#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/random.hpp>
#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/crypto/signing.hpp>

#include <string>

using namespace seventhslide::crypto;
// lock and unlock are called by their full names: a std::string argument
// brings std::lock in by argument-dependent lookup, and where <mutex> is
// included (libc++ includes it here) that template is the better match.
namespace crypto = seventhslide::crypto;

TEST_CASE("Bytes go to hex, and to base64 and base32 and back", "[crypto]")
{
    const std::string bytes{"\x00\x01\xfe\xff hello", 10};
    CHECK(to_hex(bytes) == "0001feff2068656c6c6f");

    CHECK(to_base64("") == "");
    CHECK(to_base64("f") == "Zg==");
    CHECK(to_base64("fo") == "Zm8=");
    CHECK(to_base64("foo") == "Zm9v");
    CHECK(to_base64("foobar") == "Zm9vYmFy");
    CHECK(from_base64("Zm9vYmFy") == "foobar");
    CHECK(from_base64("Zg==") == "f");
    CHECK(from_base64(to_base64(bytes)) == bytes);
    CHECK_FALSE(from_base64("Zg=").has_value());    // short
    CHECK_FALSE(from_base64("Zh==").has_value());   // stray bits
    CHECK_FALSE(from_base64("Zm9v\n").has_value());  // a line break
    CHECK_FALSE(from_base64("Zm9v!A==").has_value());

    CHECK(to_base32("") == "");
    CHECK(to_base32("f") == "MY");
    CHECK(to_base32("fo") == "MZXQ");
    CHECK(to_base32("foobar") == "MZXW6YTBOI");
    CHECK(from_base32("MZXW6YTBOI") == "foobar");
    CHECK(from_base32("mzxw-6ytb oi") == "foobar");  // as typed
    CHECK(from_base32(to_base32(bytes)) == bytes);
    CHECK_FALSE(from_base32("M").has_value());
    CHECK_FALSE(from_base32("MZ").has_value());  // stray bits
    CHECK_FALSE(from_base32("M1").has_value());  // not in the alphabet
}

TEST_CASE("SHA-256 and random bytes", "[crypto]")
{
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256("abc").size() == 32);
    CHECK(random_bytes(0).empty());
    const auto a = random_bytes(32);
    CHECK(a.size() == 32);
    CHECK(a != random_bytes(32));
}

TEST_CASE("A signing key signs, and only its public key verifies", "[crypto]")
{
    const auto key = SigningKey::generate();
    CHECK(key.seed().size() == SigningKey::k_seed_size);
    CHECK(key.public_key().bytes().size() == PublicKey::k_size);
    CHECK(key.public_key().fingerprint().size() == 64);

    const auto signature = key.sign("the message");
    CHECK(signature.size() == PublicKey::k_signature_size);
    CHECK(key.public_key().verify("the message", signature));
    CHECK_FALSE(key.public_key().verify("the messag", signature));
    CHECK_FALSE(key.public_key().verify("the message", signature + "x"));
    CHECK_FALSE(key.public_key().verify("the message", ""));
    CHECK_FALSE(SigningKey::generate().public_key().verify("the message", signature));
    // Ed25519 signatures are deterministic.
    CHECK(key.sign("the message") == signature);

    SECTION("the same key from its seed, its PEM, and the public key from base64")
    {
        const auto again = SigningKey::from_seed(key.seed());
        REQUIRE(again.has_value());
        CHECK(*again == key);
        CHECK(again->public_key() == key.public_key());
        CHECK_FALSE(SigningKey::from_seed("short").has_value());

        const auto pem = key.pem();
        CHECK(pem.starts_with("-----BEGIN PRIVATE KEY-----"));
        const auto from_pem = SigningKey::from_pem(pem);
        REQUIRE(from_pem.has_value());
        CHECK(*from_pem == key);
        CHECK_FALSE(SigningKey::from_pem("not a key").has_value());
        CHECK_FALSE(SigningKey::from_pem("-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n")
                        .has_value());

        const auto pub = PublicKey::from_base64(key.public_key().base64());
        REQUIRE(pub.has_value());
        CHECK(*pub == key.public_key());
        CHECK_FALSE(PublicKey::from_base64("AAAA").has_value());
        CHECK_FALSE(PublicKey::from_bytes("short").has_value());
    }
    SECTION("a certificate made from the key")
    {
        const auto pem = key.certificate_pem("0123456789abcdef");
        CHECK(pem.starts_with("-----BEGIN CERTIFICATE-----"));
        CHECK(pem != key.certificate_pem("0123456789abcdef"));  // a new serial each time
    }
}

TEST_CASE("A secret locked under a passphrase opens with it alone", "[crypto]")
{
    const std::string secret = random_bytes(32);
    const auto box = crypto::lock(secret, "correct horse battery staple", k_box_params_quick);
    CHECK(box.sealed.size() == 32 + 16);
    CHECK(crypto::unlock(box, "correct horse battery staple") == secret);
    CHECK_FALSE(crypto::unlock(box, "correct horse battery stapl").has_value());
    CHECK_FALSE(crypto::unlock(box, "").has_value());

    // Two lockings of one secret look nothing alike.
    CHECK(crypto::lock(secret, "correct horse battery staple", k_box_params_quick).sealed != box.sealed);

    SECTION("the box as a file, and a changed one")
    {
        const auto j = box.to_json();
        CHECK(j["kdf"] == "scrypt");
        CHECK(j["log2_n"] == 4);
        const auto back = LockedBox::from_json(j);
        REQUIRE(back.has_value());
        CHECK(*back == box);
        CHECK(crypto::unlock(*back, "correct horse battery staple") == secret);

        auto changed = box;
        changed.sealed[3] ^= 1;
        CHECK_FALSE(crypto::unlock(changed, "correct horse battery staple").has_value());

        auto bad = j;
        bad["salt"] = "AAAA";
        CHECK_FALSE(LockedBox::from_json(bad).has_value());
        bad = j;
        bad["log2_n"] = 0;
        CHECK_FALSE(LockedBox::from_json(bad).has_value());
        CHECK_FALSE(LockedBox::from_json("text").has_value());
    }
    SECTION("an empty secret")
    {
        const auto empty = crypto::lock("", "pass", k_box_params_quick);
        CHECK(crypto::unlock(empty, "pass") == std::string{});
    }
}
