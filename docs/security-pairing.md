# 配对二维码与密钥存储

本文件描述配对，以及它如何进入 FUR2 握手。配对与会话连接分成两组入口：

- **显式配对**（`pairingListen` / `pairingConnect`）只用在设置页的“扫描二维码”和“配对码匹配”两个按钮上。配对总是清掉内存里的长期密钥，让配对码或二维码密钥成为 SPAKE2 口令；成功后服务端生成新的 32 字节长期密钥，两端存入安全存储。
- **会话连接**（`sessionListen` / `sessionConnect`）只用在首页“开始”按钮。它要求两端已有共享的 32 字节长期密钥，没有密钥时直接返回 `false`，绝不降级成固定明文口令。

界面不要再散落 `listen` / `connectTo`。没有明文或 HMAC 回退。0.4.x 的 `FUR1` 会被拒绝。

## 进入握手的调用

声明在 `core/pairing/pairing_session.h`，实现在 `core/pairing/pairing_session.cpp`。

```cpp
bool pairingListen(Peer& peer, quint16 port, const PairingCredential& credential,
                   const QString& bindAddress = QString());

bool pairingConnect(Peer& peer, const QString& host, quint16 port,
                    const PairingCredential& credential,
                    const QString& localAddress = QString());
```

`credential.kind` 决定 `credential.secret` 是什么，以及实际传给 `Peer` 的口令：

| `kind` | `credential.secret` | 传给 `Peer` 的口令 | 谁在调用 |
| --- | --- | --- | --- |
| `PairingSecretKind::OneTimeQr` | 二维码字段 `s`，128 位一次性密钥的 base64url（无填充，22 字符） | `ot1.` 加上 `s` | 平板 `pairingConnect`，电脑 `pairingListen` |
| `PairingSecretKind::ManualCode` | 用户输入的手动配对码（规范化为大写） | 配对码本身。已有 `channel-long-term-key` 也不会顶替它 | 平板或电脑的 `pairingListen` / 另一端的 `pairingConnect` |

配对方向不固定：一端在设置里按“配对码匹配”（或电脑按“显示二维码”）进入监听，另一端用同一个码（或扫码）连接。手动配对码永远不读 `channel-long-term-key`；旧的长期密钥只在会话连接里用。

口令不会发到线路上，也不会按 `HMAC(challenge, password)` 发送。`Peer::listen` / `Peer::connectTo` / `sendVideo` 的名字不变。

一次性密钥用过即废。扫描侧用 `pairingConsumeOneTimeSecret` 拒绝第二次使用，二维码过期时关掉电脑上的监听。`Peer` 对 `ot1.` 口令再做一次进程内拒绝：成功握手之后，同一进程再次传入同一口令会失败。成功后的 32 字节长期密钥写入安全存储的 `channel-long-term-key`，下次手动配对前经 `setLongTermKey` 使用。

### 会话连接

```cpp
bool sessionListen(Peer& peer, quint16 port, const QString& bindAddress = QString());
bool sessionConnect(Peer& peer, const QString& host, quint16 port,
                    const QString& localAddress = QString());
```

- 密钥来源：先用 `Peer::longTermKey()`（本进程刚配对出来的），再用 `SecretStore::instance().load("channel-long-term-key")`。
- 有 32 字节密钥时 `setLongTermKey(key)`，然后用占位 token `FUR2-LTK` 调 `listen` / `connectTo`。密钥存在时 token 只作占位，SPAKE2 口令是那把密钥。
- 没有密钥时返回 `false`，不碰传输层。绝不在无密钥时用一个公开常量当口令 —— 那样局域网内任何人都能连上。

调用点：

- 电脑配对码匹配：`Fukaros/ui/mainwindow_settings.cpp` 的 `MainWindow::pairWithCode` 调 `pairingConnect`；IP 不可用或 4 秒超时后转 `LanDiscovery` 广播搜索，再逐个 `pairingConnect`。
- 电脑显示二维码：`Fukaros/ui/mainwindow_settings.cpp` 的 `MainWindow::showPairingQr` 调用 `pairingListen(m_peer, payload.port, credential, payload.host)`。
- 平板配对码匹配：`Furina/ui/mainwindow_settings.cpp` 的 `MainWindow::pairWithCode` 调 `pairingListen(m_peer, 27183, credential, "0.0.0.0")`。
- 平板扫码：`MainWindow::pairFromQrPayload` 在固定指纹之后调用 `pairingConnect(m_peer, decoded.payload.host, decoded.payload.port, credential)`。
- 两端首页“开始”：`sessionListen` / `sessionConnect`。配对成功后 `Peer::connected` 只存密钥并关闭连接，提示回首页按开始；二维码配对同样不自动发起投屏。

## 局域网发现

Wi-Fi 模式下，电脑先连设置里的 IP，4 秒连不上就调用 `Fukaros/core/transport/lan_discovery.cpp` 的 `LanDiscovery`：在每个以太网或 Wi-Fi 网卡上向网关和广播地址发 `FURINA_DISCOVER_V1`（端口 27184），收集所有应答（平板由 `NetworkDiscoveryServer` 照原接口回发），去重后逐个尝试连接。全部失败时明确提示“局域网内没有找到可连接的平板”。

该模块只处理 `IF_TYPE_ETHERNET_CSMACD` 和 `IF_TYPE_IEEE80211`（Wi-Fi）网卡，并排除 RNDIS / Remote NDIS / NCM 描述和 `USB\` 设备，与 `UsbNetwork` 的 USB 路径互不串台。发现报文本身不含密钥，只声明端口 27183 可连。

## 长期公钥指纹

二维码字段 `f` 是桌面长期公钥的 SHA-256，64 个小写十六进制字符。计算函数是 `pairingPublicKeyFingerprint`。

公钥来源是 `pairingDesktopPublicKey(SecretStore&)`。它保存 32 字节 X25519 私钥，键名 `desktop-x25519-secret`，并用 `furinaX25519PublicKey` 算出公钥。指纹函数仍是 `pairingPublicKeyFingerprint`，不改。

平板在 `pairingConnect` 之前调用 `pairingPinPublicKey`。同一主机已固定的指纹若不一致，则拒绝连接，此时还没有发出流量。

## 二维码文本

```text
fukaros://pair?v=2&h=HOST&p=PORT&s=ONE_TIME_SECRET&f=FINGERPRINT&e=UNIX_EXPIRY
```

- `e` 是绝对过期时间。剩余时间必须大于 0 且不超过 120 秒。
- 过期、篡改、版本不是 2、密钥或指纹格式不对，都会在 `pairingDecodePayload` 失败。
- 同一密钥的第二次 `pairingConsumeOneTimeSecret` 失败。落盘的是哈希和过期时间，不是密钥本身。
- 电脑显示二维码，平板相机扫描。手动配对码仍然可用。

手动配对码规则在 `pairingCheckManualCode`：至少 6 位；字母表 `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`，即任意英文字母和数字，I/O 与 0/1 都可以用；整段不能是同一个字符。不再限制必须同时包含字母和数字，也不拒绝顺子。界面在输入时提示，开始连接时再检查一次。位数下限对应 `Peer` 传输层 6 字节的 token 下限。`Peer` 仍接受测试直接注入的短 token。

## 存储

密钥不写入明文 `QSettings` 键 `token`。读到旧的 `token` 时，`pairingMigratePlaintext` 写入安全存储后删除该键。密封失败时保留原值，避免把唯一一份密钥丢掉。

- Windows：`CryptProtectData` / `CryptUnprotectData`，当前用户范围（不使用 `CRYPTPROTECT_LOCAL_MACHINE`），不弹界面。密文放在 `QSettings` 的 `secure/` 下。
- Android：`Furina/android/src/com/furina/black/SecretBox.java` 使用 Android Keystore 的 AES-GCM。密文在应用私有文件 `pairing-secrets.bin`，不进偏好设置。清单里 `android:allowBackup` 仍为 false。
- 其他系统上的桌面模拟只把密钥留在进程内存里，不写盘。这不是 Windows / Android 的正式存储。

日志、二维码调试输出和界面提示都不包含 `s` 或配对码内容。
