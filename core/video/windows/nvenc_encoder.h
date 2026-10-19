#pragma once
#include "backend_types.h"
#include "video_policy.h"

namespace furina::video::detail {
class NvEncoder {
    QLibrary library;
    NV_ENCODE_API_FUNCTION_LIST functions{};
    void* encoder = nullptr;
    NV_ENC_REGISTERED_PTR registered = nullptr;
    NV_ENC_OUTPUT_PTR output = nullptr;
    ID3D11Texture2D* input = nullptr;
    int width = 0, height = 0;
    uint64_t sequence = 0;

  public:
    int lastQp = 0;
    QString error;

    ~NvEncoder() {
        clear();
    }

    void clear() {
        if (encoder) {
            if (registered)
                functions.nvEncUnregisterResource(encoder, registered);
            if (output)
                functions.nvEncDestroyBitstreamBuffer(encoder, output);
            functions.nvEncDestroyEncoder(encoder);
        }
        encoder = nullptr;
        registered = nullptr;
        output = nullptr;
        input = nullptr;
    }

    bool open(ID3D11Device* device, ID3D11Texture2D* texture, int w, int h) {
        clear();
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        library.setFileName(QString::fromWCharArray(system) + "/nvEncodeAPI64.dll");
        if (!library.load()) {
            error = "NVENC driver API unavailable";
            return false;
        }
        auto create = reinterpret_cast<NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(
            library.resolve("NvEncodeAPICreateInstance"));
        functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        if (!create || create(&functions) != NV_ENC_SUCCESS) {
            error = "NVENC API version unavailable";
            return false;
        }
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session{};
        session.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        session.device = device;
        session.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        session.apiVersion = NVENCAPI_VERSION;
        if (functions.nvEncOpenEncodeSessionEx(&session, &encoder) != NV_ENC_SUCCESS) {
            error = "NVENC session unavailable";
            return false;
        }
        NV_ENC_PRESET_CONFIG preset{};
        preset.version = NV_ENC_PRESET_CONFIG_VER;
        preset.presetCfg.version = NV_ENC_CONFIG_VER;
        if (functions.nvEncGetEncodePresetConfigEx(encoder, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P3_GUID,
                                                   NV_ENC_TUNING_INFO_LOW_LATENCY,
                                                   &preset) != NV_ENC_SUCCESS) {
            error = "NVENC preset unavailable";
            clear();
            return false;
        }
        NV_ENC_CONFIG config = preset.presetCfg;
        config.version = NV_ENC_CONFIG_VER;
        config.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
        config.gopLength = FR_VIDEO_GOP;
        config.frameIntervalP = 1;
        // A one-frame CBR budget forced periodic IDRs to QP 50/51, blurring text.
        // Give intra frames a short burst budget and cap quantization, without B frames
        // or lookahead. VBV is a rate-control budget, not a queued playback delay.
        config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        config.rcParams.averageBitRate = fr_video_bitrate(w, h);
        config.rcParams.maxBitRate = config.rcParams.averageBitRate * 3 / 2;
        config.rcParams.vbvBufferSize = config.rcParams.averageBitRate / 5;
        config.rcParams.vbvInitialDelay = config.rcParams.vbvBufferSize;
        config.rcParams.targetQuality = 22;
        config.rcParams.enableMaxQP = 1;
        config.rcParams.maxQP.qpIntra = 28;
        config.rcParams.maxQP.qpInterP = 30;
        config.rcParams.maxQP.qpInterB = 30;
        config.rcParams.enableLookahead = 0;
        config.rcParams.enableAQ = 0;
        config.rcParams.zeroReorderDelay = 1;
        config.encodeCodecConfig.h264Config.idrPeriod = FR_VIDEO_GOP;
        config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
        // Declare the RGB -> YUV matrix so Android and PC decode the same colors.
        auto& vui = config.encodeCodecConfig.h264Config.h264VUIParameters;
        vui.videoSignalTypePresentFlag = 1;
        vui.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
        vui.videoFullRangeFlag = 0;
        vui.colourDescriptionPresentFlag = 1;
        vui.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        vui.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        vui.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;

        NV_ENC_INITIALIZE_PARAMS init{};
        init.version = NV_ENC_INITIALIZE_PARAMS_VER;
        init.encodeGUID = NV_ENC_CODEC_H264_GUID;
        init.presetGUID = NV_ENC_PRESET_P3_GUID;
        init.encodeWidth = w;
        init.encodeHeight = h;
        init.darWidth = w;
        init.darHeight = h;
        init.frameRateNum = 60;
        init.frameRateDen = 1;
        init.enablePTD = 1;
        init.encodeConfig = &config;
        init.maxEncodeWidth = w;
        init.maxEncodeHeight = h;
        init.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
        auto result = functions.nvEncInitializeEncoder(encoder, &init);
        if (result != NV_ENC_SUCCESS) {
            error = QString("NVENC initialize %1").arg(result);
            clear();
            return false;
        }
        NV_ENC_REGISTER_RESOURCE resource{};
        resource.version = NV_ENC_REGISTER_RESOURCE_VER;
        resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        resource.resourceToRegister = texture;
        resource.width = w;
        resource.height = h;
        resource.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
        resource.bufferUsage = NV_ENC_INPUT_IMAGE;
        result = functions.nvEncRegisterResource(encoder, &resource);
        if (result != NV_ENC_SUCCESS) {
            error = QString("NVENC register %1").arg(result);
            clear();
            return false;
        }
        registered = resource.registeredResource;
        NV_ENC_CREATE_BITSTREAM_BUFFER buffer{};
        buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        result = functions.nvEncCreateBitstreamBuffer(encoder, &buffer);
        if (result != NV_ENC_SUCCESS) {
            error = "NVENC bitstream buffer";
            clear();
            return false;
        }
        output = buffer.bitstreamBuffer;
        input = texture;
        width = w;
        height = h;
        return true;
    }

    bool encode(bool key, QByteArray* bytes, bool* isKey) {
        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = registered;
        auto result = functions.nvEncMapInputResource(encoder, &map);
        if (result != NV_ENC_SUCCESS) {
            error = "NVENC map";
            return false;
        }
        NV_ENC_PIC_PARAMS picture{};
        picture.version = NV_ENC_PIC_PARAMS_VER;
        picture.inputBuffer = map.mappedResource;
        picture.bufferFmt = map.mappedBufferFmt;
        picture.inputWidth = width;
        picture.inputHeight = height;
        picture.outputBitstream = output;
        picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        picture.inputTimeStamp = sequence++;
        picture.inputDuration = 1;
        if (key)
            picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
        result = functions.nvEncEncodePicture(encoder, &picture);
        if (result != NV_ENC_SUCCESS) {
            functions.nvEncUnmapInputResource(encoder, map.mappedResource);
            error = QString("NVENC encode %1").arg(result);
            return false;
        }
        NV_ENC_LOCK_BITSTREAM lock{};
        lock.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = output;
        result = functions.nvEncLockBitstream(encoder, &lock);
        if (result == NV_ENC_SUCCESS) {
            lastQp = int(lock.frameAvgQP);
            *bytes = QByteArray(static_cast<const char*>(lock.bitstreamBufferPtr), lock.bitstreamSizeInBytes);
            *isKey = lock.pictureType == NV_ENC_PIC_TYPE_IDR || lock.pictureType == NV_ENC_PIC_TYPE_I;
            functions.nvEncUnlockBitstream(encoder, output);
        }
        functions.nvEncUnmapInputResource(encoder, map.mappedResource);
        if (result != NV_ENC_SUCCESS) {
            error = "NVENC output lock";
            return false;
        }
        return true;
    }
};

} // namespace furina::video::detail
