#include "lan_addresses.hpp"

#include <QHostAddress>
#include <QNetworkInterface>

namespace seventhslide::app {

QStringList lan_addresses()
{
    QStringList out;
    for (const QHostAddress& addr : QNetworkInterface::allAddresses()) {
        if (addr.isLoopback() || addr.isLinkLocal()
            || addr.protocol() != QAbstractSocket::IPv4Protocol) {
            continue;
        }
        out.push_back(addr.toString());
    }
    out.removeDuplicates();
    return out;
}

}  // namespace seventhslide::app
