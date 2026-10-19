#include "desktop_video.h"
#include "video_policy.h"
#include "debug_log.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QElapsedTimer>
#include <QBuffer>
#include <QTransform>
#include <chrono>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include "windows/gpu_capture.h"
#include "windows/nvenc_encoder.h"
#include "windows/mf_encoder.h"
#include "windows/h264_decoder.h"
using namespace furina::video::detail;
#endif
DesktopVideoSender::DesktopVideoSender(QObject* parent) : QObject(parent) {}

DesktopVideoSender::~DesktopVideoSender() {
    stop();
}

void DesktopVideoSender::setResolution(int edge) {
    if (fr_resolution_valid(edge))
        m_resolution = edge;
}

void DesktopVideoSender::feedback(double fps, unsigned queueMs, double workMs) {
    m_remoteFps = fps;
    m_queueMs = queueMs;
    m_remoteWorkMs = workMs;
}

void DesktopVideoSender::accepted(bool delivered) {
    m_pending = false;
    if (!delivered)
        m_key = true;
}

void DesktopVideoSender::start(const QString& display, const QRect& geometry) {
    Q_UNUSED(geometry);
    stop();
    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        m_display = display;
    }
    m_stop = false;
    m_refresh = false;
    m_key = true;
    m_pending = false;
    m_remoteFps = 60;
    m_queueMs = 0;
    m_remoteWorkMs = 0;
    m_localDecodeMs = 0;
    const quint64 token = ++m_runToken;
    m_thread = QThread::create([this] { run(); });
    connect(
        m_thread, &QThread::finished, this,
        [this, token] {
            if (token == m_runToken && !m_stop)
                emit failed();
        },
        Qt::QueuedConnection);
    m_thread->start();
}

void DesktopVideoSender::refreshDisplay(const QString& display) {
    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        m_display = display;
    }
    m_refresh = true;
}

void DesktopVideoSender::stop() {
    ++m_runToken;
    m_stop = true;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_pending = false;
}

void DesktopVideoSender::run() {
#ifdef Q_OS_WIN
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        auto& a = api();
        if (!a.failure.isEmpty()) {
            emit status(
                furinaStatus("视频组件不可用：" + a.failure, "Video component unavailable: " + a.failure));
        } else {
            fr_video_policy policy;
            fr_video_policy_init(&policy, m_resolution);
            QElapsedTimer clock, recovery;
            clock.start();
            recovery.start();
            uint32_t sequence = 0;
            bool fatal = false, announced = false;
            // Display changes invalidate DXGI duplication, D3D surfaces and NVENC registration.
            // Recreate the whole capture/encoder pair while retaining the peer and monotonic PTS.
            while (!m_stop && !fatal) {
                m_refresh.exchange(false);
                QString display;
                {
                    std::lock_guard<std::mutex> lock(m_sourceMutex);
                    display = m_display;
                }
                Capture capture;
                if (!capture.open(display)) {
                    if (recovery.elapsed() > 10000) {
                        emit status(furinaStatus("显示器采集恢复超时：" + capture.error,
                                                 "Display capture recovery timed out: " + capture.error));
                        break;
                    }
                    if (!announced) {
                        emit status(furinaStatus(
                            "显示器暂不可用，正在等待显示配置稳定。",
                            "Display temporarily unavailable. Waiting for the configuration to settle."));
                        announced = true;
                    }
                    QThread::msleep(100);
                    continue;
                }
                Encoder encoder;
                NvEncoder nv;
                bool native = true, retry = false, currentMapping = false;
                int currentEdge = 0, w = 0, h = 0, frames = 0;
                uint32_t epoch = 0;
                double work = 0;
                QElapsedTimer sample;
                sample.start();
                qint64 due = clock.nsecsElapsed();
                while (!m_stop) {
                    if (m_refresh.exchange(false)) {
                        retry = true;
                        break;
                    }
                    if (recovery.elapsed() > 10000) {
                        emit status(furinaStatus("显示器未能恢复实时画面，采集已停止。",
                                                 "The monitor did not resume live frames. Capture stopped."));
                        fatal = true;
                        break;
                    }
                    const auto now = clock.nsecsElapsed();
                    if (now < due) {
                        QThread::usleep(qMin<qint64>((due - now) / 1000, 2000));
                        continue;
                    }
                    due += 1000000000LL / 60;
                    if (now - due > 1000000000LL / 60)
                        due = now + 1000000000LL / 60;
                    if (m_resolution != policy.selected)
                        fr_video_policy_init(&policy, m_resolution);
                    if (m_pending)
                        continue;
                    const bool mapping = m_separateCursor.load();
                    if (mapping != currentMapping) {
                        currentMapping = mapping;
                        currentEdge = 0;
                    }
                    capture.setSeparateCursor(mapping);
                    QElapsedTimer processing;
                    processing.start();
                    std::vector<QByteArray> output;
                    std::vector<bool> keys;
                    if (policy.edge != currentEdge) {
                        nv.clear();
                        encoder.clear();
                        fr_video_dimensions(capture.sourceSize().width(), capture.sourceSize().height(),
                                            policy.edge, &w, &h);
                        auto* texture = native ? capture.gpuFrame(w, h) : nullptr;
                        if (native && !texture) {
                            if (capture.error.isEmpty())
                                continue;
                            if (capture.recoverable) {
                                retry = true;
                                break;
                            }
                            native = false;
                        }
                        if (native && !nv.open(capture.gpuDevice(), texture, w, h)) {
                            native = false;
                            emit status(furinaStatus("NVENC 不可用，切换 Media Foundation：" + nv.error,
                                                     "NVENC unavailable; switching to Media Foundation: " +
                                                         nv.error));
                        }
                        if (!native && !encoder.open(w, h)) {
                            emit status(furinaStatus("硬件编码启动失败：" + encoder.error,
                                                     "Hardware encoder could not start: " + encoder.error));
                            fatal = true;
                            break;
                        }
                        currentEdge = policy.edge;
                        epoch = ++m_epoch;
                        if (!epoch)
                            epoch = ++m_epoch;
                        m_key = true;
                        emit status(furinaStatus(QString("%1 + %2，%3×%4，目标 60 FPS")
                                                     .arg(capture.backend())
                                                     .arg(native ? "GPU 缩放 + NVENC" : "Media Foundation")
                                                     .arg(w)
                                                     .arg(h),
                                                 QString("%1 + %2, %3x%4, target 60 FPS")
                                                     .arg(capture.backend())
                                                     .arg(native ? "GPU scaling + NVENC" : "Media Foundation")
                                                     .arg(w)
                                                     .arg(h)));
                    }
                    if (native) {
                        if (!capture.gpuFrame(w, h)) {
                            if (capture.error.isEmpty())
                                continue;
                            if (capture.recoverable) {
                                retry = true;
                                break;
                            }
                            emit status(furinaStatus("采集停止：" + capture.error,
                                                     "Capture stopped: " + capture.error));
                            fatal = true;
                            break;
                        }
                        QByteArray data;
                        bool key = false;
                        if (!nv.encode(m_key.exchange(false), &data, &key)) {
                            retry = true;
                            break;
                        }
                        output.push_back(data);
                        keys.push_back(key);
                    } else {
                        auto image = capture.grab();
                        if (image.isNull()) {
                            if (capture.error.isEmpty())
                                continue;
                            if (capture.recoverable) {
                                retry = true;
                                break;
                            }
                            emit status(furinaStatus("采集停止：" + capture.error,
                                                     "Capture stopped: " + capture.error));
                            fatal = true;
                            break;
                        }
                        if (!encoder.encode(image, m_key.exchange(false), &output, &keys)) {
                            emit status(furinaStatus("编码失败：" + encoder.error,
                                                     "Encoding failed: " + encoder.error));
                            fatal = true;
                            break;
                        }
                    }
                    auto shape = capture.takeCursorShape();
                    if (!shape.isEmpty())
                        emit cursorShape(shape);
                    work += processing.nsecsElapsed() / 1000000.0;
                    for (size_t i = 0; i < output.size(); ++i) {
                        if (output[i].isEmpty())
                            continue;
                        fr_video_info info{(keys[i] ? FR_VIDEO_KEY : 0u) |
                                               (capture.separateCursor() ? FR_VIDEO_CURSOR_SEPARATE : 0u),
                                           ++sequence,
                                           uint32_t(w),
                                           uint32_t(h),
                                           epoch,
                                           uint64_t(clock.nsecsElapsed() / 1000)};
                        QByteArray packet(FR_VIDEO_HEADER, Qt::Uninitialized);
                        fr_video_write(reinterpret_cast<uint8_t*>(packet.data()), &info);
                        packet += output[i];
                        m_pending = true;
                        emit this->packet(packet);
                        ++frames;
                        recovery.restart();
                        if (announced) {
                            emit status(furinaStatus(
                                "显示配置变化后的实时采集已恢复。",
                                "Live capture recovered after the display configuration change."));
                            announced = false;
                        }
                    }
                    if (sample.elapsed() >= 1000) {
                        const auto elapsed = sample.elapsed();
                        double fps = frames * 1000.0 / elapsed, ms = frames ? work / frames : 0;
                        emit stats(fps, ms, currentEdge);
                        fr_video_policy_sample_elapsed(
                            &policy, qMin(fps, m_remoteFps.load()),
                            qMax(ms + m_localDecodeMs.load(), m_remoteWorkMs.load()), m_queueMs,
                            uint32_t(qMin<qint64>(elapsed, 60000)));
                        frames = 0;
                        work = 0;
                        sample.restart();
                    }
                }
                if (!retry || m_stop)
                    break;
                if (!announced) {
                    emit status(
                        furinaStatus("显示配置变化，正在重建 GPU 采集与编码。",
                                     "Display configuration changed. Recreating GPU capture and encoding."));
                    announced = true;
                }
                m_pending = false;
                QThread::msleep(100);
            }
        }
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    timeEndPeriod(1);
#else
    emit status(furinaStatus("桌面视频采集仅在 Windows 使用", "Desktop capture is available on Windows"));
#endif
}

DesktopVideoReceiver::DesktopVideoReceiver(QObject* parent) : QObject(parent) {
    m_thread = QThread::create([this] { run(); });
    m_thread->start();
}

DesktopVideoReceiver::~DesktopVideoReceiver() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread->wait();
    delete m_thread;
}

void DesktopVideoReceiver::reset() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_generation;
        m_reset = true;
        m_packets.clear();
        m_latest = {};
        m_waitKey = true;
        m_notifyPending = false;
    }
    m_wake.notify_all();
}

void DesktopVideoReceiver::submit(const QByteArray& data) {
    fr_video_info info{};
    if (!fr_video_read(reinterpret_cast<const uint8_t*>(data.constData()), data.size(), &info))
        return;
    bool recovery = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_packets.size() >= 12) {
            m_packets.clear();
            m_waitKey = true;
            m_reset = true;
            recovery = true;
        }
        if (!m_waitKey || (info.flags & FR_VIDEO_KEY)) {
            m_waitKey = false;
            m_packets.push_back(data);
        }
    }
    if (recovery)
        emit recover();
    m_wake.notify_one();
}

bool DesktopVideoReceiver::takeImage(QImage* image) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notifyPending = false;
    if (m_latest.isNull())
        return false;
    *image = std::move(m_latest);
    m_latest = {};
    return true;
}

void DesktopVideoReceiver::run() {
#ifdef Q_OS_WIN
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        auto& a = api();
        if (!a.failure.isEmpty()) {
            emit status(
                furinaStatus("解码组件不可用：" + a.failure, "Decoder component unavailable: " + a.failure));
        } else {
            Decoder decoder;
            uint32_t epoch = 0;
            int frames = 0;
            double work = 0;
            unsigned queue = 0;
            QElapsedTimer sample;
            sample.start();
            while (true) {
                QByteArray bytes;
                bool reset = false;
                uint64_t generation = 0;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_wake.wait(lock, [this] { return m_stop || m_reset || !m_packets.empty(); });
                    if (m_stop)
                        break;
                    generation = m_generation;
                    reset = m_reset;
                    m_reset = false;
                    if (m_packets.empty()) {
                        if (reset) {
                            decoder.clear();
                            epoch = 0;
                            frames = 0;
                            work = 0;
                            sample.restart();
                        }
                        continue;
                    }
                    bytes = std::move(m_packets.front());
                    m_packets.pop_front();
                    queue = unsigned(m_packets.size() * 1000 / 60);
                }
                fr_video_info info{};
                if (!fr_video_read(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size(), &info))
                    continue;
                if (reset || !decoder.codec || epoch != info.epoch) {
                    if (!(info.flags & FR_VIDEO_KEY))
                        continue;
                    if (reset && decoder.codec && epoch == info.epoch) {
                        a.avcodec_flush_buffers(decoder.codec);
                    } else if (!decoder.open()) {
                        emit status(furinaStatus("解码启动失败：" + decoder.error,
                                                 "Decoder could not start: " + decoder.error));
                        continue;
                    }
                    if (!epoch)
                        sample.restart();
                    epoch = info.epoch;
                    emit status(furinaStatus(decoder.hardware ? "电脑 H.264 解码使用 D3D11VA"
                                                              : "电脑 H.264 解码使用软件回退",
                                             decoder.hardware ? "PC H.264 decoding uses D3D11VA"
                                                              : "PC H.264 decoding uses software fallback"));
                }
                QElapsedTimer processing;
                processing.start();
                QImage image;
                if (!decoder.decode(bytes.mid(FR_VIDEO_HEADER), &image)) {
                    emit status(furinaStatus("视频解码失败：" + decoder.error,
                                             "Video decode failed: " + decoder.error));
                    emit recover();
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_waitKey = true;
                        m_reset = true;
                    }
                    continue;
                }
                work += processing.nsecsElapsed() / 1000000.0;
                bool notify = false;
                if (!image.isNull()) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (generation != m_generation)
                        continue;
                    m_latest = std::move(image);
                    ++frames;
                    if (!m_notifyPending) {
                        m_notifyPending = true;
                        notify = true;
                    }
                }
                if (notify)
                    emit frameReady();
                if (sample.elapsed() >= 1000) {
                    emit stats(frames * 1000.0 / sample.elapsed(), frames ? work / frames : 0, queue);
                    frames = 0;
                    work = 0;
                    sample.restart();
                }
            }
        }
    }
    if (SUCCEEDED(com))
        CoUninitialize();
#else
    std::unique_lock<std::mutex> lock(m_mutex);
    m_wake.wait(lock, [this] { return m_stop; });
#endif
}
