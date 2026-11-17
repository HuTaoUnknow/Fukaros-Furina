#include "lan_discovery.h"
#include "network_discovery.h"
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QSettings>
#include <vector>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#endif

#ifdef Q_OS_WIN
// The mirror image of UsbNetwork's filter: physical/RNDIS-excluded LAN adapters.
// Wi-Fi adapters report IF_TYPE_IEEE80211, not IF_TYPE_ETHERNET_CSMACD, so both
// must be accepted or the search finds nothing on a Wi-Fi-only machine.
static bool lanAdapter(const IP_ADAPTER_ADDRESSES* a) {
    if ((a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) ||
        a->OperStatus != IfOperStatusUp)
        return false;
    const auto description = QString::fromWCharArray(a->Description);
    if (description.contains("RNDIS", Qt::CaseInsensitive) ||
        description.contains("Remote NDIS", Qt::CaseInsensitive) ||
        description.contains("NCM", Qt::CaseInsensitive))
        return false;
    QSettings connection(
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\" +
            QString::fromLatin1(a->AdapterName) + "\\Connection",
        QSettings::NativeFormat);
    return !connection.value("PnpInstanceID").toString().startsWith("USB\\", Qt::CaseInsensitive);
}

static QHostAddress socketAddress(const SOCKET_ADDRESS& address) {
    if (!address.lpSockaddr || address.lpSockaddr->sa_family != AF_INET)
        return {};
    return QHostAddress(ntohl(reinterpret_cast<const sockaddr_in*>(address.lpSockaddr)->sin_addr.s_addr));
}
#endif

static bool sameSubnet(const QHostAddress& host, const LanInterface& adapter) {
    if (host.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    const auto mask = adapter.netmask.toIPv4Address();
    return mask && (host.toIPv4Address() & mask) == (adapter.local.toIPv4Address() & mask);
}

QList<LanInterface> furinaLanInterfaces() {
    QList<LanInterface> result;
#ifdef Q_OS_WIN
    ULONG size = 16384;
    std::vector<unsigned char> data(size);
    ULONG error;
    for (int attempt = 0; attempt < 3; ++attempt) {
        error = GetAdaptersAddresses(AF_INET,
                                     GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_DNS_SERVER |
                                         GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST,
                                     nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()), &size);
        if (error != ERROR_BUFFER_OVERFLOW)
            break;
        data.resize(size);
    }
    if (error != NO_ERROR)
        return result;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data()); a; a = a->Next) {
        if (!lanAdapter(a))
            continue;
        QHostAddress gateway;
        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            gateway = socketAddress(g->Address);
            if (!gateway.isNull())
                break;
        }
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const auto local = socketAddress(u->Address);
            const auto ip = local.toIPv4Address();
            const auto prefix = u->OnLinkPrefixLength;
            if (local.isNull() || local.isLoopback() || local.isMulticast() ||
                (ip & 0xffff0000) == 0xa9fe0000 || prefix == 0 || prefix > 30)
                continue;
            const quint32 mask = 0xffffffffu << (32 - prefix);
            result.append({QString::fromWCharArray(a->FriendlyName), local, QHostAddress(mask),
                           QHostAddress(ip | ~mask), gateway, a->IfIndex});
        }
    }
#endif
    return result;
}

LanDiscovery::LanDiscovery(QObject* parent, Interfaces interfaces)
    : QObject(parent), m_interfaces(std::move(interfaces)) {
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &LanDiscovery::probe);
    m_settle.setSingleShot(true);
    m_settle.setInterval(250);
    connect(&m_settle, &QTimer::timeout, this, &LanDiscovery::finish);
}

void LanDiscovery::closeSockets() {
    for (auto* socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();
}

void LanDiscovery::cancel() {
    m_running = false;
    m_timer.stop();
    m_settle.stop();
    m_hosts.clear();
    m_nonce.clear();
    closeSockets();
}

void LanDiscovery::start(int timeoutMs, quint16 port) {
    cancel();
    m_lastCount = -1;
    m_timeout = qMax(1, timeoutMs);
    m_port = port;
    for (int i = 0; i < 4; ++i)
        m_nonce += QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    m_running = true;
    m_elapsed.start();
    m_timer.start();
    probe();
}

void LanDiscovery::probe() {
    if (!m_running)
        return;
    if (m_elapsed.elapsed() >= m_timeout) {
        finish();
        return;
    }
    const auto interfaces = m_interfaces();
    if (m_lastCount != interfaces.size()) {
        m_lastCount = interfaces.size();
        emit status(interfaces.isEmpty() ? "没有可用的局域网网卡，请在设置中选择 Wi-Fi 或 USB。"
                                         : "正在局域网内搜索平板。",
                    interfaces.isEmpty() ? "No LAN adapter is available. Choose Wi-Fi or USB in Settings."
                                         : "Searching the local network for the tablet.");
    }
    const auto sockets = m_sockets;
    for (auto* socket : sockets) {
        bool exists = false;
        for (const auto& adapter : interfaces)
            if (adapter.local == socket->localAddress())
                exists = true;
        if (!exists) {
            m_sockets.removeOne(socket);
            socket->close();
            socket->deleteLater();
        }
    }
    for (const auto& adapter : interfaces) {
        QUdpSocket* socket = nullptr;
        for (auto* candidate : m_sockets)
            if (candidate->localAddress() == adapter.local)
                socket = candidate;
        if (!socket) {
            socket = new QUdpSocket(this);
            socket->setProxy(QNetworkProxy::NoProxy);
            if (!socket->bind(adapter.local, 0)) {
                socket->deleteLater();
                continue;
            }
            m_sockets.append(socket);
            const auto nonce = m_nonce;
            connect(socket, &QUdpSocket::readyRead, this, [this, socket, adapter, nonce] {
                while (socket->hasPendingDatagrams()) {
                    const auto reply = socket->receiveDatagram(128);
                    const auto host = reply.senderAddress();
                    if (!m_running || nonce != m_nonce || reply.senderPort() != m_port ||
                        !furinaDiscoveryMatches(reply.data(), nonce) || host == adapter.local ||
                        !sameSubnet(host, adapter) || host == adapter.broadcast)
                        continue;
                    const auto text = host.toString();
                    if (!m_hosts.contains(text))
                        m_hosts.append(text);
                    if (!m_settle.isActive())
                        m_settle.start();
                }
            });
        }
        const auto send = [&](const QHostAddress& destination) {
            if (destination.isNull())
                return;
            QNetworkDatagram packet(furinaDiscoveryRequest(m_nonce), destination, m_port);
            packet.setInterfaceIndex(adapter.index);
            socket->writeDatagram(packet);
        };
        send(adapter.gateway);
        send(adapter.broadcast);
    }
}

void LanDiscovery::finish() {
    if (!m_running)
        return;
    const auto found = m_hosts;
    cancel();
    if (found.isEmpty())
        emit failed("局域网内没有找到可连接的平板。请确认两端在同一网络，并已在设置中完成配对。",
                    "No tablet was found on the local network. Make sure both devices share the same network "
                    "and have completed pairing in Settings.");
    else
        emit hosts(found);
}