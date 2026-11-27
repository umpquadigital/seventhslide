#pragma once

// TLS between two SeventhSlides, from their computer keys: each side shows a
// certificate made from its key, and the other reads the key back out of it.
// No certificate authority is involved; which keys are welcome is the member
// list's question, asked once the handshake has proved which key is on the
// other end.
//
// Seam: Qt Network and the crypto module (OpenSSL, to hand Qt the key: its
// own PEM reader does not take an Ed25519 key).

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/sync/identity.hpp"

#include <QSslCertificate>
#include <QSslConfiguration>

#include <optional>

namespace seventhslide::link {

/// Makes Qt's OpenSSL backend the one its TLS uses, and says whether it
/// could. ``tls_configuration`` hands Qt an OpenSSL key object, an Ed25519
/// certificate and TLS 1.3, which only that backend takes; Qt's default is
/// another one on Windows (Schannel) and macOS (Secure Transport). Qt keeps
/// to the first backend anything uses, so this is called before any TLS,
/// HTTPS included: at the start of ``main``, as well as here. Once per
/// process; the application must exist (Qt finds the backend as a plugin).
[[nodiscard]] bool use_openssl_tls();

/// TLS 1.3 as ``me``: this computer's certificate and key, asking the peer
/// for its certificate without judging it (``QueryPeer``); ``peer_key``
/// then says who it is. Throws ``std::runtime_error`` when the key cannot
/// be loaded, or Qt has no OpenSSL backend (``use_openssl_tls``).
[[nodiscard]] QSslConfiguration tls_configuration(const sync::Identity& me);

/// The Ed25519 key a certificate was made from, or ``std::nullopt`` for a
/// certificate that is not one of ours.
[[nodiscard]] std::optional<crypto::PublicKey> peer_key(const QSslCertificate& certificate);

}  // namespace seventhslide::link
