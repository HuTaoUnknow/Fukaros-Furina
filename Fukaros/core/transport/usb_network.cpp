#include <QtGlobal>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#endif
#include "usb_network.h"
#include "network_discovery.h"
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QSettings>
#include <QByteArray>
#include <vector>
#include <utility>
#ifdef Q_OS_WIN
static bool usbAdapter(const IP_ADAPTER_ADDRESSES* a) {
    if (a->IfType != IF_TYPE_ETHERNET_CSMACD || a->OperStatus != IfOperStatusUp)
        return false;
    const auto description = QString::fromWCharArray(a->Description);
    // Windows' RNDIS/NCM class drivers identify Android USB tethering. The
    // registry also covers OEM USB Ethernet drivers with different names.
    if (description.contains("RNDIS", Qt::CaseInsensitive) ||
        description.contains("Remote NDIS", Qt::CaseInsensitive) ||
        description.contains("NCM", Qt::CaseInsensitive))
        return true;
    QSettings connection(
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\" +
            QString::fromLatin1(a->AdapterName) + "\\Connection",
        QSettings::NativeFormat);
    return connection.value("PnpInstanceID").toString().startsWith("USB\\", Qt::CaseInsensitive);
}

static QHostAddress socketAddress(const SOCKET_ADDRESS& address) {
    if (!address.lpSockaddr || address.lpSockaddr->sa_family != AF_INET)
        return {};
    return QHostAddress(ntohl(reinterpret_cast<const sockaddr_in*>(address.lpSockaddr)->sin_addr.s_addr));
}
#endif
static bool sameSubnet(const QHostAddress& host, const UsbNetworkInterface& adapter) {
    if (host.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    const auto mask = adapter.netmask.toIPv4Address();
    return mask && (host.toIPv4Address() & mask) == (adapter.local.toIPv4Address() & mask);
}

QList<UsbNetworkInterface> furinaUsbNetworkInterfaces() {
    QList<UsbNetworkInterface> result;
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
        if (!usbAdapter(a))
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

UsbNetwork::UsbNetwork(QObject* parent, Interfaces interfaces)
    : QObject(parent), m_interfaces(std::move(interfaces)) {
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &UsbNetwork::probe);
    m_settle.setSingleShot(true);
    m_settle.setInterval(250);
    connect(&m_settle, &QTimer::timeout, this, &UsbNetwork::finish);
}

void UsbNetwork::cancel() {
    m_running = false;
    m_timer.stop();
    m_settle.stop();
    m_matches.clear();
    for (auto* socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();
    m_nonce.clear();
}

void UsbNetwork::start(const QString& host, int timeoutMs, quint16 port) {
    cancel();
    m_host = {};
    m_lastCount = -1;
    m_timeout = qMax(1, timeoutMs);
    m_port = port;
    if (!host.trimmed().isEmpty() &&
        (!m_host.setAddress(host.trimmed()) || m_host.protocol() != QAbstractSocket::IPv4Protocol ||
         m_host.isLoopback() || m_host.isMulticast() || m_host.toIPv4Address() == 0 ||
         m_host.toIPv4Address() == 0xffffffffu)) {
        emit failed(
            "USB 平板 IP 无效，请填写 USB 网卡网段中的 IPv4 地址，或留空自动发现。",
            "Invalid USB tablet IP. Use an IPv4 address on the USB subnet, or leave it empty for discovery.");
        return;
    }
    for (int i = 0; i < 4; ++i)
        m_nonce += QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    m_running = true;
    m_elapsed.start();
    m_timer.start();
    probe();
}

void UsbNetwork::probe() {
    if (!m_running)
        return;
    if (m_elapsed.elapsed() >= m_timeout) {
        cancel();
        emit failed(
            "USB 连接超时。请连接数据线、打开平板 USB 网络共享，并在 Furina 按开始；也可手填 USB IP。",
            "USB connection timed out. Connect the cable, enable USB tethering and press Start in Furina; alternatively enter its USB IP.");
        return;
    }
    const auto interfaces = m_interfaces();
    if (m_lastCount != interfaces.size()) {
        m_lastCount = interfaces.size();
        emit status(interfaces.isEmpty() ? "等待 USB 网卡，请在平板打开 USB 网络共享。"
                                         : "已发现 USB 网卡，正在寻找 Furina。",
                    interfaces.isEmpty()
                        ? "Waiting for a USB network adapter. Enable USB tethering on the tablet."
                        : "USB network adapter found. Looking for Furina.");
    }
    // Keep sockets alive across probes so delayed replies remain usable. A
    // source address removed by unplugging is discarded on the next probe.
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
        if (!m_host.isNull()) {
            if (m_host != adapter.local && sameSubnet(m_host, adapter) && m_host != adapter.broadcast) {
                const auto host = m_host.toString(), local = adapter.local.toString(), name = adapter.name;
                cancel();
                emit found(host, local, name);
                return;
            }
            continue;
        }
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
                    bool exists = false;
                    for (const auto& match : m_matches)
                        if (match.host == host && match.adapter.local == adapter.local)
                            exists = true;
                    if (!exists)
                        m_matches.append({host, adapter});
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

void UsbNetwork::finish() {
    if (!m_running || m_matches.isEmpty())
        return;
    if (m_matches.size() > 1) {
        cancel();
        emit failed("发现多个 USB Furina，请在 USB 设置中填写目标平板的 USB IP。",
                    "Multiple USB Furina devices found. Enter the target tablet's USB IP in Settings.");
        return;
    }
    const auto match = m_matches.first();
    cancel();
    emit found(match.host.toString(), match.adapter.local.toString(), match.adapter.name);
}
