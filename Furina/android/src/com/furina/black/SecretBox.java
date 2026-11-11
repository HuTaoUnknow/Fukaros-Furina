package com.furina.black;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.LinkedHashMap;
import java.util.Map;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Android Keystore AES-GCM wrapper. Ciphertext stays in the app's private files, never in preferences. */
public final class SecretBox {
    private static final String ALIAS = "furina-pairing";
    private static final String FILE_NAME = "pairing-secrets.bin";
    private static final int MAGIC = 0x46534543;
    private static final int VERSION = 1;

    private SecretBox() {}

    public static synchronized boolean store(String key, byte[] plain) {
        if (key == null || key.isEmpty() || plain == null || plain.length == 0)
            return false;
        try {
            Map<String, byte[]> all = readAll();
            all.put(key, encrypt(plain));
            writeAll(all);
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    public static synchronized byte[] load(String key) {
        if (key == null || key.isEmpty())
            return null;
        try {
            byte[] sealed = readAll().get(key);
            if (sealed == null)
                return null;
            return decrypt(sealed);
        } catch (Exception ignored) {
            return null;
        }
    }

    public static synchronized boolean remove(String key) {
        if (key == null || key.isEmpty())
            return false;
        try {
            Map<String, byte[]> all = readAll();
            if (all.remove(key) == null && !file().exists())
                return true;
            writeAll(all);
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    private static File file() {
        FurinaActivity activity = FurinaActivity.currentActivity();
        if (activity == null)
            throw new IllegalStateException("activity");
        return new File(activity.getFilesDir(), FILE_NAME);
    }

    private static SecretKey secretKey() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(ALIAS)) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(
                    ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .setRandomizedEncryptionRequired(true)
                    .build());
            generator.generateKey();
            store.load(null);
        }
        return (SecretKey) store.getKey(ALIAS, null);
    }

    private static byte[] encrypt(byte[] plain) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, secretKey());
        byte[] iv = cipher.getIV();
        byte[] body = cipher.doFinal(plain);
        ByteArrayOutputStream out = new ByteArrayOutputStream(iv.length + body.length);
        out.write(iv.length);
        out.write(iv);
        out.write(body);
        return out.toByteArray();
    }

    private static byte[] decrypt(byte[] sealed) throws Exception {
        if (sealed.length < 2)
            throw new IllegalArgumentException("sealed");
        int ivLength = sealed[0] & 0xff;
        if (ivLength < 12 || sealed.length < 1 + ivLength + 16)
            throw new IllegalArgumentException("iv");
        byte[] iv = new byte[ivLength];
        System.arraycopy(sealed, 1, iv, 0, ivLength);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, secretKey(), new GCMParameterSpec(128, iv));
        return cipher.doFinal(sealed, 1 + ivLength, sealed.length - 1 - ivLength);
    }

    private static Map<String, byte[]> readAll() throws Exception {
        Map<String, byte[]> all = new LinkedHashMap<>();
        File path = file();
        if (!path.exists())
            return all;
        try (DataInputStream in = new DataInputStream(new FileInputStream(path))) {
            if (in.readInt() != MAGIC || in.readInt() != VERSION)
                return all;
            int count = in.readInt();
            for (int i = 0; i < count; i++) {
                int keyLength = in.readInt();
                byte[] keyBytes = new byte[keyLength];
                in.readFully(keyBytes);
                int valueLength = in.readInt();
                byte[] value = new byte[valueLength];
                in.readFully(value);
                all.put(new String(keyBytes, StandardCharsets.UTF_8), value);
            }
        }
        return all;
    }

    private static void writeAll(Map<String, byte[]> all) throws Exception {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(buffer);
        out.writeInt(MAGIC);
        out.writeInt(VERSION);
        out.writeInt(all.size());
        for (Map.Entry<String, byte[]> entry : all.entrySet()) {
            byte[] key = entry.getKey().getBytes(StandardCharsets.UTF_8);
            out.writeInt(key.length);
            out.write(key);
            out.writeInt(entry.getValue().length);
            out.write(entry.getValue());
        }
        out.flush();
        File path = file();
        File temporary = new File(path.getParentFile(), FILE_NAME + ".tmp");
        try (FileOutputStream file = new FileOutputStream(temporary)) {
            file.write(buffer.toByteArray());
            file.getFD().sync();
        }
        if (path.exists() && !path.delete())
            throw new IllegalStateException("replace");
        if (!temporary.renameTo(path))
            throw new IllegalStateException("rename");
    }
}
