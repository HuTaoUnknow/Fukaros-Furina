#pragma once
#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>

template <class T> struct TransformCom {
    T* p = nullptr;

    ~TransformCom() {
        clear();
    }

    void clear() {
        if (p)
            p->Release();
        p = nullptr;
    }

    T** put() {
        clear();
        return &p;
    }

    T* operator->() const {
        return p;
    }
};

// DXGI supplies an unrotated surface even when the desktop is portrait.
// Rotate and scale in one RGB shader pass before encoding; the cursor is added afterwards.
class DesktopTransform {
    TransformCom<ID3D11VertexShader> vs;
    TransformCom<ID3D11PixelShader> ps;
    TransformCom<ID3D11Buffer> constants;
    TransformCom<ID3D11SamplerState> sampler;
    TransformCom<ID3D11RasterizerState> raster;
    TransformCom<ID3D11ShaderResourceView> input;
    TransformCom<ID3D11RenderTargetView> output;
    ID3D11Texture2D *lastInput = nullptr, *lastOutput = nullptr;

    bool init(ID3D11Device* device) {
        if (ps.p && constants.p && sampler.p && raster.p)
            return true;
        const char* code = R"(
cbuffer Transform:register(b0){float4 config;};
Texture2D<float4> source:register(t0);SamplerState linearClamp:register(s0);
float4 vertex(uint id:SV_VertexID):SV_POSITION{return float4(id==2?3:-1,id==1?3:-1,0,1);}
float4 pixel(float4 pos:SV_POSITION):SV_TARGET{
 float2 uv=pos.xy/config.xy;
 if(config.z==1)uv=float2(uv.y,1-uv.x);
 else if(config.z==2)uv=1-uv;
 else if(config.z==3)uv=float2(1-uv.y,uv.x);
 return float4(source.Sample(linearClamp,uv).rgb,1);
})";
        TransformCom<ID3DBlob> v, p, errors;
        if (FAILED(D3DCompile(code, strlen(code), nullptr, nullptr, nullptr, "vertex", "vs_4_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, v.put(), errors.put())) ||
            FAILED(D3DCompile(code, strlen(code), nullptr, nullptr, nullptr, "pixel", "ps_4_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, p.put(), errors.put())))
            return false;
        if (FAILED(
                device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, vs.put())) ||
            FAILED(device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, ps.put())))
            return false;
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = 16;
        b.Usage = D3D11_USAGE_DEFAULT;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC s{};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC r{};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_NONE;
        r.DepthClipEnable = TRUE;
        return SUCCEEDED(device->CreateBuffer(&b, nullptr, constants.put())) &&
               SUCCEEDED(device->CreateSamplerState(&s, sampler.put())) &&
               SUCCEEDED(device->CreateRasterizerState(&r, raster.put()));
    }

  public:
    bool draw(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src, ID3D11Texture2D* dst,
              int w, int h, unsigned turns) {
        if (!init(device))
            return false;
        if (src != lastInput) {
            if (FAILED(device->CreateShaderResourceView(src, nullptr, input.put())))
                return false;
            lastInput = src;
        }
        if (dst != lastOutput) {
            if (FAILED(device->CreateRenderTargetView(dst, nullptr, output.put())))
                return false;
            lastOutput = dst;
        }
        float params[] = {float(w), float(h), float(turns % 4), 0};
        context->UpdateSubresource(constants.p, 0, nullptr, params, 0, 0);
        D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(raster.p);
        context->OMSetRenderTargets(1, &output.p, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.p, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(ps.p, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &constants.p);
        context->PSSetSamplers(0, 1, &sampler.p);
        context->PSSetShaderResources(0, 1, &input.p);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        return true;
    }
};
#endif
