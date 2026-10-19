#pragma once
#include "backend_types.h"
#include "video_policy.h"

namespace furina::video::detail {
class Capture {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGIOutputDuplication> duplicate;
    Com<ID3D11Texture2D> staging;
    Com<ID3D11Texture2D> sourceTexture, scaledTexture;
    Com<ID3D11VideoDevice> videoDevice;
    Com<ID3D11VideoContext> videoContext;
    Com<ID3D11VideoProcessorEnumerator> enumerator;
    Com<ID3D11VideoProcessor> processor;
    Com<ID3D11VideoProcessorInputView> inputView;
    Com<ID3D11VideoProcessorOutputView> outputView;
    int sourceW = 0, sourceH = 0, scaledW = 0, scaledH = 0;
    QImage latest;
    bool separateRequested = false, usingWgc = false, wgcBlocked = false;
    HMONITOR monitor = nullptr;
    unsigned activeTurns = 0;
    FurinaWgc::Capture wgc;
    DesktopCursor cursor;
    DesktopTransform transform;
    unsigned turns = 0;

    QImage cpuFrame() {
        if (latest.isNull())
            return {};
        QImage frame =
            activeTurns ? latest.transformed(QTransform().rotate(90 * activeTurns)) : latest.copy();
        if (!separateCursor())
            cursor.drawCpu(frame.bits(), frame.width(), frame.height(), frame.bytesPerLine());
        return frame;
    }

  public:
    QString error;

    void setSeparateCursor(bool value) {
        separateRequested = value;
    }

    bool separateCursor() const {
        return usingWgc || (separateRequested && cursor.separateVisible());
    }

    QString backend() const {
        return usingWgc ? "WGC" : "DXGI";
    }

    QByteArray takeCursorShape() {
        auto data = cursor.takeShape();
        return QByteArray(reinterpret_cast<const char*>(data.data()), data.size());
    }

    bool recoverable = false;

    ID3D11Device* gpuDevice() const {
        return device.p;
    }

    QSize sourceSize() const {
        return QSize(sourceW, sourceH);
    }

    bool open(const QString& name) {
        Com<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put())))) {
            error = "DXGI factory";
            return false;
        }
        for (UINT a = 0;; ++a) {
            Com<IDXGIAdapter1> adapter;
            const auto adapterResult = factory->EnumAdapters1(a, adapter.put());
            if (adapterResult == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(adapterResult)) {
                error = "DXGI adapter enumeration failed";
                return false;
            }
            for (UINT o = 0;; ++o) {
                Com<IDXGIOutput> output;
                const auto outputResult = adapter->EnumOutputs(o, output.put());
                if (outputResult == DXGI_ERROR_NOT_FOUND)
                    break;
                if (FAILED(outputResult)) {
                    error = "DXGI output enumeration failed";
                    return false;
                }
                DXGI_OUTPUT_DESC desc{};
                if (FAILED(output->GetDesc(&desc)))
                    continue;
                if (!desc.AttachedToDesktop || QString::fromWCharArray(desc.DeviceName) != name)
                    continue;
                if (FAILED(D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                             device.put(), nullptr, context.put()))) {
                    error = "D3D11 device";
                    return false;
                }
                Com<IDXGIOutput1> modern;
                if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1),
                                                  reinterpret_cast<void**>(modern.put())))) {
                    error = "DXGI output";
                    return false;
                }
                const auto hr = modern->DuplicateOutput(device.p, duplicate.put());
                if (FAILED(hr)) {
                    error = QString("DuplicateOutput HRESULT %1").arg(quint32(hr), 8, 16, QChar('0'));
                    return false;
                }
                DXGI_OUTDUPL_DESC d{};
                duplicate->GetDesc(&d);
                sourceW = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
                sourceH = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
                turns = d.Rotation >= DXGI_MODE_ROTATION_ROTATE90
                            ? unsigned(d.Rotation - DXGI_MODE_ROTATION_IDENTITY)
                            : 0;
                monitor = desc.Monitor;
                return true;
            }
        }
        error = "Display not found: " + name;
        return false;
    }

    ID3D11Texture2D* gpuFrame(int w, int h) {
        if (!acquire())
            return nullptr;
        if (activeTurns) {
            if (scaledW != w || scaledH != h) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = w;
                desc.Height = h;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                if (FAILED(device->CreateTexture2D(&desc, nullptr, scaledTexture.put()))) {
                    error = "GPU portrait texture failed";
                    return nullptr;
                }
                scaledW = w;
                scaledH = h;
            }
            if (!transform.draw(device.p, context.p, sourceTexture.p, scaledTexture.p, w, h, activeTurns)) {
                error = "GPU portrait rotation failed";
                return nullptr;
            }
            if (!separateCursor() &&
                !cursor.draw(device.p, context.p, scaledTexture.p, sourceW, sourceH, w, h)) {
                error = "GPU cursor composition failed";
                return nullptr;
            }
            context->Flush();
            return scaledTexture.p;
        }
        if (scaledW != w || scaledH != h) {
            outputView.clear();
            inputView.clear();
            processor.clear();
            enumerator.clear();
            scaledTexture.clear();
            device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(videoDevice.put()));
            context->QueryInterface(__uuidof(ID3D11VideoContext),
                                    reinterpret_cast<void**>(videoContext.put()));
            if (!videoDevice || !videoContext) {
                error = "GPU video processor unavailable";
                return nullptr;
            }
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
            content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            content.InputWidth = sourceW;
            content.InputHeight = sourceH;
            content.OutputWidth = w;
            content.OutputHeight = h;
            content.InputFrameRate = {60, 1};
            content.OutputFrameRate = {60, 1};
            content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&content, enumerator.put())) ||
                FAILED(videoDevice->CreateVideoProcessor(enumerator.p, 0, processor.put()))) {
                error = "GPU scaler initialization failed";
                return nullptr;
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, scaledTexture.put()))) {
                error = "GPU scaler texture failed";
                return nullptr;
            }
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};
            input.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output{};
            output.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            if (FAILED(videoDevice->CreateVideoProcessorInputView(sourceTexture.p, enumerator.p, &input,
                                                                  inputView.put())) ||
                FAILED(videoDevice->CreateVideoProcessorOutputView(scaledTexture.p, enumerator.p, &output,
                                                                   outputView.put()))) {
                error = "GPU scaler views failed";
                return nullptr;
            }
            RECT source{0, 0, sourceW, sourceH}, dest{0, 0, w, h};
            videoContext->VideoProcessorSetStreamFrameFormat(processor.p, 0,
                                                             D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
            videoContext->VideoProcessorSetStreamSourceRect(processor.p, 0, TRUE, &source);
            videoContext->VideoProcessorSetStreamDestRect(processor.p, 0, TRUE, &dest);
            videoContext->VideoProcessorSetOutputTargetRect(processor.p, TRUE, &dest);
            scaledW = w;
            scaledH = h;
        }
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView.p;
        if (FAILED(videoContext->VideoProcessorBlt(processor.p, outputView.p, 0, 1, &stream))) {
            error = "GPU scaling failed";
            return nullptr;
        }
        if (!separateCursor() && !cursor.draw(device.p, context.p, scaledTexture.p, sourceW, sourceH, w, h)) {
            error = "GPU cursor composition failed";
            return nullptr;
        }
        context->Flush();
        return scaledTexture.p;
    }

    QImage grab() {
        if (!acquire())
            return {};
        if (!sourceTexture)
            return {};
        D3D11_TEXTURE2D_DESC desc{};
        sourceTexture->GetDesc(&desc);
        if (!staging) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            device->CreateTexture2D(&desc, nullptr, staging.put());
        }
        if (staging) {
            context->CopyResource(staging.p, sourceTexture.p);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging.p, 0, D3D11_MAP_READ, 0, &mapped))) {
                latest = QImage(int(desc.Width), int(desc.Height), QImage::Format_RGB32);
                for (int y = 0; y < latest.height(); ++y)
                    memcpy(latest.scanLine(y), static_cast<const char*>(mapped.pData) + y * mapped.RowPitch,
                           latest.width() * 4);
                context->Unmap(staging.p, 0);
            }
        }
        return cpuFrame();
    }

  private:
    bool acquire() {
        bool requested = separateRequested && !wgcBlocked;
        if (requested && !wgc.opened() && !wgc.open(device.p, monitor)) {
            wgcBlocked = true;
            requested = false;
        }
        if (requested != usingWgc) {
            outputView.clear();
            inputView.clear();
            processor.clear();
            enumerator.clear();
            scaledTexture.clear();
            sourceTexture.clear();
            staging.clear();
            latest = {};
            scaledW = scaledH = 0;
            usingWgc = requested;
            if (!requested)
                wgc.close();
        }
        activeTurns = usingWgc ? 0 : turns;
        Com<ID3D11Texture2D> texture;
        if (usingWgc) {
            if (!wgc.next(device.p, context.p, texture.put())) {
                recoverable = true;
                error = wgc.error;
                return false;
            }
            if (texture) {
                sourceTexture.clear();
                sourceTexture.p = texture.p;
                texture.p = nullptr;
            }
            return bool(sourceTexture);
        }
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        auto hr = duplicate->AcquireNextFrame(0, &info, resource.put());
        if (SUCCEEDED(hr)) {
            bool pointerOk = cursor.update(device.p, duplicate.p, info);
            resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.put()));
            if (texture) {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                if (!sourceTexture) {
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    desc.MiscFlags = 0;
                    desc.CPUAccessFlags = 0;
                    device->CreateTexture2D(&desc, nullptr, sourceTexture.put());
                }
                if (sourceTexture)
                    context->CopyResource(sourceTexture.p, texture.p);
            }
            duplicate->ReleaseFrame();
            if (!pointerOk) {
                recoverable = true;
                error = "DXGI pointer shape changed during capture";
                return false;
            }
        } else if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
            recoverable = hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED ||
                          hr == DXGI_ERROR_DEVICE_RESET;
            error = QString("DXGI capture HRESULT %1").arg(quint32(hr), 8, 16, QChar('0'));
            return false;
        }
        return bool(sourceTexture);
    }
};

} // namespace furina::video::detail
