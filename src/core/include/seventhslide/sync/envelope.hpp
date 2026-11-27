#pragma once

// A signed JSON document, as everything in the store is kept: a first line
// saying who signed it and how, then the document (``body``) as it was
// written, byte for byte:
//
//     {"format":2,"signatures":[{"as":"computer","key":"<base64>","signature":"<base64>"}]}
//     {...the body...}
//
// A signature covers exactly the body's bytes after that line, so a reader
// checks what it was given, never what its own JSON library would write of
// it: computers with another nlohmann/json, or another way of writing
// numbers, agree on every signature. The body is read once, and is readable
// as it is.
//
// A change file or snapshot has one signature, the writing computer's; a
// member list entry has the computer's and, for admin work, the admin
// key's (or the recovery key's). Whether a key is the right one for its
// role is the reader's question (the member list); this file answers only
// whether each signature is that key's.
//
// Seam: nlohmann/json and the crypto module.

#include "seventhslide/crypto/signing.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::sync {

inline constexpr int k_envelope_format = 2;

/// The roles a signature is given as.
inline constexpr std::string_view k_signed_as_computer = "computer";
inline constexpr std::string_view k_signed_as_admin = "admin";
inline constexpr std::string_view k_signed_as_recovery = "recovery";
/// A join entry's proof that its writer holds the invite (``member_list.hpp``).
inline constexpr std::string_view k_signed_as_invite = "invite";

struct Signature {
    std::string as;
    crypto::PublicKey key;
};

struct Envelope {
    nlohmann::json body;
    /// Each checked: the key did sign the body.
    std::vector<Signature> signatures;

    /// The key that signed as ``as``, or null.
    [[nodiscard]] const crypto::PublicKey* signed_as(std::string_view as) const;
};

/// ``body`` signed by each key as its role, as the text to keep.
[[nodiscard]] std::string seal(const nlohmann::json& body,
                               const std::vector<std::pair<std::string_view, const crypto::SigningKey*>>& signers);

/// Read and check ``text``. Throws ``NewerFormat`` (``sync_store.hpp``) for
/// an envelope of a later format, and ``std::runtime_error`` when it is not
/// an envelope, or any signature is not its key's; ``what`` names the kind
/// of file in the message ("A sync change file").
[[nodiscard]] Envelope unseal(std::string_view text, std::string_view what);

}  // namespace seventhslide::sync
