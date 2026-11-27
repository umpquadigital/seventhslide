#include "seventhslide/link/tls.hpp"

#include <QByteArray>
#include <QSsl>
#include <QSslKey>
#include <QSslSocket>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace seventhslide::link {

bool use_openssl_tls()
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = QSslSocket::setActiveBackend(QStringLiteral("openssl")); });
    return ok;
}

QSslConfiguration tls_configuration(const sync::Identity& me)
{
    if (!use_openssl_tls()) {
        throw std::runtime_error(
            "Sync needs Qt's OpenSSL TLS (the tls plugin's qopensslbackend, and OpenSSL 3), "
            "which this SeventhSlide does not have");
    }
    const auto pem = me.key.pem();
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    EVP_PKEY* pkey = bio != nullptr ? PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(bio);
    if (pkey == nullptr) {
        throw std::runtime_error("This computer's sync key could not be loaded for TLS");
    }
    // Qt takes the key object as its own and frees it.
    const QSslKey key(Qt::HANDLE(pkey), QSsl::PrivateKey);
    const auto certificates = QSslCertificate::fromData(
        QByteArray::fromStdString(me.key.certificate_pem(me.machine)), QSsl::Pem);
    if (key.isNull() || certificates.size() != 1) {
        throw std::runtime_error("This computer's certificate could not be made for TLS");
    }

    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setProtocol(QSsl::TlsV1_3OrLater);
    config.setLocalCertificate(certificates.front());
    config.setPrivateKey(key);
    // The peer's certificate is asked for and kept, not judged against any
    // authority: it is self-signed, as ours is.
    config.setPeerVerifyMode(QSslSocket::QueryPeer);
    return config;
}

std::optional<crypto::PublicKey> peer_key(const QSslCertificate& certificate)
{
    if (certificate.isNull()) {
        return std::nullopt;
    }
    const QByteArray der = certificate.toDer();
    const auto* data = reinterpret_cast<const unsigned char*>(der.constData());
    X509* x509 = d2i_X509(nullptr, &data, der.size());
    if (x509 == nullptr) {
        return std::nullopt;
    }
    std::optional<crypto::PublicKey> out;
    if (EVP_PKEY* pkey = X509_get0_pubkey(x509); pkey != nullptr && EVP_PKEY_is_a(pkey, "ED25519")) {
        std::string raw(crypto::PublicKey::k_size, '\0');
        std::size_t size = raw.size();
        if (EVP_PKEY_get_raw_public_key(pkey, reinterpret_cast<unsigned char*>(raw.data()), &size)
                == 1
            && size == raw.size()) {
            out = crypto::PublicKey::from_bytes(raw);
        }
    }
    X509_free(x509);
    return out;
}

}  // namespace seventhslide::link
