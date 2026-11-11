# 配对二维码与密钥存储

本文件描述二维码配对，以及它如何进入 FUR2 握手。界面只调用 `pairingListen` / `pairingConnect`，不要再散落 `listen` / `connectTo`。没有明文或 HMAC 回退。0.4.x 的 `FUR1` 会被拒绝。

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
| `PairingSecretKind::OneTimeQr` | 二维码字段 `s`，128 位一次性密钥的 base64url（无填充，22 字符） | `ot1.` 加上 `s`。这次握手会先清掉长期密钥 | 电脑 `pairingListen`，平板 `pairingConnect` |
| `PairingSecretKind::ManualCode` | 用户输入的手动配对码（规范化为大写） | 尚无长期密钥时，配对码本身是 SPAKE2 口令。已有 `channel-long-term-key` 时改用那 32 字节 | 平板 `pairingListen`，电脑 `pairingConnect` |

口令不会发到线路上，也不会按 `HMAC(challenge, password)` 发送。`Peer::listen` / `Peer::connectTo` / `sendVideo` 的名字不变。

一次性密钥用过即废。扫描侧用 `pairingConsumeOneTimeSecret` 拒绝第二次使用，二维码过期时关掉电脑上的监听。`Peer` 对 `ot1.` 口令再做一次进程内拒绝：成功握手之后，同一进程再次传入同一口令会失败。成功后的 32 字节长期密钥写入安全存储的 `channel-long-term-key`，下次手动配对前经 `setLongTermKey` 使用。

调用点：

- 电脑手动连接：`Fukaros/ui/mainwindow_session.cpp` 里的 `pairingConnect(...)`。
- 电脑显示二维码：`Fukaros/ui/mainwindow_settings.cpp` 的 `MainWindow::showPairingQr` 调用 `pairingListen(m_peer, payload.port, credential, payload.host)`。
- 平板手动开服务：`Furina/ui/mainwindow_session.cpp` 的 `pairingListen(m_peer, 27183, credential, bind)`。
- 平板扫码：`MainWindow::pairFromQrPayload` 在固定指纹之后调用 `pairingConnect(m_peer, decoded.payload.host, decoded.payload.port, credential)`。

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

手动配对码规则在 `pairingCheckManualCode`：至少 12 位；字母表 `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`；至少各有一个字母和一个数字；整段不能是同一个字符；任意连续 4 位不能是该字母表上的递增或递减序列（例如 `ABCD`、`2345`）。界面在输入时提示，开始连接时再检查一次。`Peer` 仍接受测试直接注入的短 token。

## 存储

密钥不写入明文 `QSettings` 键 `token`。读到旧的 `token` 时，`pairingMigratePlaintext` 写入安全存储后删除该键。密封失败时保留原值，避免把唯一一份密钥丢掉。

- Windows：`CryptProtectData` / `CryptUnprotectData`，当前用户范围（不使用 `CRYPTPROTECT_LOCAL_MACHINE`），不弹界面。密文放在 `QSettings` 的 `secure/` 下。
- Android：`Furina/android/src/com/furina/black/SecretBox.java` 使用 Android Keystore 的 AES-GCM。密文在应用私有文件 `pairing-secrets.bin`，不进偏好设置。清单里 `android:allowBackup` 仍为 false。
- 其他系统上的桌面模拟只把密钥留在进程内存里，不写盘。这不是 Windows / Android 的正式存储。

日志、二维码调试输出和界面提示都不包含 `s` 或配对码内容。
