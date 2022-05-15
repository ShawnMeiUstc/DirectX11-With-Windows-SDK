#include "Effects.h"
#include "XUtil.h"
#include "RenderStates.h"
#include "EffectHelper.h"
#include "DXTrace.h"
#include "Vertex.h"
#include "TextureManager.h"

using namespace DirectX;

# pragma warning(disable: 26812)

//
// ShadowEffect::Impl 需要先于ShadowEffect的定义
//

class ShadowEffect::Impl
{
public:
    // 必须显式指定
    Impl() {}
    ~Impl() = default;

public:
    std::unique_ptr<EffectHelper> m_pEffectHelper;

    std::shared_ptr<IEffectPass> m_pCurrEffectPass;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_pVertexPosNormalTexLayout;

    XMFLOAT4X4 m_World{}, m_View{}, m_Proj{};
};

//
// ShadowEffect
//

namespace
{
    // ShadowEffect单例
    static ShadowEffect* g_pInstance = nullptr;
}

ShadowEffect::ShadowEffect()
{
    if (g_pInstance)
        throw std::exception("ShadowEffect is a singleton!");
    g_pInstance = this;
    pImpl = std::make_unique<ShadowEffect::Impl>();
}

ShadowEffect::~ShadowEffect()
{
}

ShadowEffect::ShadowEffect(ShadowEffect&& moveFrom) noexcept
{
    pImpl.swap(moveFrom.pImpl);
}

ShadowEffect& ShadowEffect::operator=(ShadowEffect&& moveFrom) noexcept
{
    pImpl.swap(moveFrom.pImpl);
    return *this;
}

ShadowEffect& ShadowEffect::Get()
{
    if (!g_pInstance)
        throw std::exception("ShadowEffect needs an instance!");
    return *g_pInstance;
}

bool ShadowEffect::InitAll(ID3D11Device* device)
{
    if (!device)
        return false;

    if (!RenderStates::IsInit())
        throw std::exception("RenderStates need to be initialized first!");

    pImpl->m_pEffectHelper = std::make_unique<EffectHelper>();

    Microsoft::WRL::ComPtr<ID3DBlob> blob;

    // ******************
    // 创建顶点着色器
    //

    HR(pImpl->m_pEffectHelper->CreateShaderFromFile("ShadowVS", L"Shaders\\Shadow.hlsl", 
        device, "ShadowVS", "vs_5_0", nullptr, blob.GetAddressOf()));
    // 创建顶点布局
    HR(device->CreateInputLayout(VertexPosNormalTex::GetInputLayout(), ARRAYSIZE(VertexPosNormalTex::GetInputLayout()),
        blob->GetBufferPointer(), blob->GetBufferSize(), pImpl->m_pVertexPosNormalTexLayout.GetAddressOf()));
    HR(pImpl->m_pEffectHelper->CreateShaderFromFile("FullScreenTriangleTexcoordVS", L"Shaders\\Shadow.hlsl",
        device, "FullScreenTriangleTexcoordVS", "vs_5_0"));

    // ******************
    // 创建像素着色器
    //

    const char* msaa_strs[] = { "1", "2", "4", "8" };
    D3D_SHADER_MACRO defines[] = {
        "MSAA_SAMPLES", "1",
        nullptr, nullptr
    };
    HR(pImpl->m_pEffectHelper->CreateShaderFromFile("ShadowPS", L"Shaders\\Shadow.hlsl",
        device, "ShadowPS", "ps_5_0"));
    HR(pImpl->m_pEffectHelper->CreateShaderFromFile("DebugPS", L"Shaders\\Shadow.hlsl",
        device, "DebugPS", "ps_5_0"));
    HR(pImpl->m_pEffectHelper->CreateShaderFromFile("ExponentialShadowPS", L"Shaders\\Shadow.hlsl",
        device, "ExponentialShadowPS", "ps_5_0"));

    // ******************
    // 创建通道
    //
    EffectPassDesc passDesc;
    passDesc.nameVS = "ShadowVS";
    HR(pImpl->m_pEffectHelper->AddEffectPass("DepthOnly", device, &passDesc));
    auto pPass = pImpl->m_pEffectHelper->GetEffectPass("DepthOnly");
    pPass->SetRasterizerState(RenderStates::RSShadow.Get());

    passDesc.namePS = "ShadowPS";
    HR(pImpl->m_pEffectHelper->AddEffectPass("Shadow", device, &passDesc));
    pPass = pImpl->m_pEffectHelper->GetEffectPass("Shadow");
    pPass->SetRasterizerState(RenderStates::RSShadow.Get());

    std::string psName = "VarianceShadowPS_1xMSAA";
    std::string passName = "VarianceShadow_1xMSAA";
    for (const char * str : msaa_strs)
    {
        defines[0].Definition = str;
        passName[15] = *str;
        psName[17] = *str;

        HR(pImpl->m_pEffectHelper->CreateShaderFromFile(psName, L"Shaders\\Shadow.hlsl",
            device, "VarianceShadowPS", "ps_5_0", defines));

        passDesc.nameVS = "FullScreenTriangleTexcoordVS";
        passDesc.namePS = psName;
        HR(pImpl->m_pEffectHelper->AddEffectPass(passName, device, &passDesc));
    }

    passDesc.nameVS = "FullScreenTriangleTexcoordVS";
    passDesc.namePS = "ExponentialShadowPS";
    HR(pImpl->m_pEffectHelper->AddEffectPass("ExponentialShadow", device, &passDesc));

    passDesc.nameVS = "FullScreenTriangleTexcoordVS";
    passDesc.namePS = "DebugPS";
    HR(pImpl->m_pEffectHelper->AddEffectPass("Debug", device, &passDesc));

    const char* kernel_strs[] = {
        "3", "5", "7", "9", "11", "13", "15"
    };

    passDesc.nameVS = "FullScreenTriangleTexcoordVS";
    defines[0].Name = "BLUR_KERNEL_SIZE";
    for (const char* str : kernel_strs)
    {
        defines[0].Definition = str;

        psName = "VSMVerticalBlurPS_";
        psName += str;
        HR(pImpl->m_pEffectHelper->CreateShaderFromFile(psName, L"Shaders\\Shadow.hlsl",
            device, "VSMVerticalBlurPS", "ps_5_0", defines));
        passDesc.namePS = psName;
        passName = "VSMVerticalBlur_";
        passName += str;
        HR(pImpl->m_pEffectHelper->AddEffectPass(passName, device, &passDesc));


        psName = "VSMHorizontialBlurPS_";
        psName += str;
        HR(pImpl->m_pEffectHelper->CreateShaderFromFile(psName, L"Shaders\\Shadow.hlsl",
            device, "VSMHorizontialBlurPS", "ps_5_0", defines));
        passDesc.namePS = psName;
        passName = "VSMHorizontialBlur_";
        passName += str;
        HR(pImpl->m_pEffectHelper->AddEffectPass(passName, device, &passDesc));


        psName = "ESMLogGaussianBlurPS_";
        psName += str;
        HR(pImpl->m_pEffectHelper->CreateShaderFromFile(psName, L"Shaders\\Shadow.hlsl",
            device, "ESMLogGaussianBlurPS", "ps_5_0", defines));
        passDesc.namePS = psName;
        passName = "ESMLogGaussianBlur_";
        passName += str;
        HR(pImpl->m_pEffectHelper->AddEffectPass(passName, device, &passDesc));
    }
    
    pImpl->m_pEffectHelper->SetSamplerStateByName("g_SamplerPointClamp", RenderStates::SSPointClamp.Get());

    // 设置调试对象名
#if (defined(DEBUG) || defined(_DEBUG)) && (GRAPHICS_DEBUGGER_OBJECT_NAME)
    pImpl->m_pVertexPosNormalTexLayout->SetPrivateData(WKPDID_D3DDebugObjectName, LEN_AND_STR("ShadowEffect.VertexPosNormalTexLayout"));
#endif
    pImpl->m_pEffectHelper->SetDebugObjectName("ShadowEffect");

    return true;
}

void ShadowEffect::SetRenderDepthOnly(ID3D11DeviceContext* deviceContext)
{
    deviceContext->IASetInputLayout(pImpl->m_pVertexPosNormalTexLayout.Get());
    pImpl->m_pCurrEffectPass = pImpl->m_pEffectHelper->GetEffectPass("DepthOnly");
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ShadowEffect::SetRenderDefault(ID3D11DeviceContext* deviceContext)
{
    deviceContext->IASetInputLayout(pImpl->m_pVertexPosNormalTexLayout.Get());
    pImpl->m_pCurrEffectPass = pImpl->m_pEffectHelper->GetEffectPass("Shadow");
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ShadowEffect::RenderVarianceShadow(ID3D11DeviceContext* deviceContext, 
    ID3D11ShaderResourceView* input, 
    ID3D11RenderTargetView* output, 
    const D3D11_VIEWPORT& vp)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pTex;
    D3D11_TEXTURE2D_DESC texDesc;
    input->GetResource(reinterpret_cast<ID3D11Resource**>(pTex.GetAddressOf()));
    pTex->GetDesc(&texDesc);

    std::string passName = "VarianceShadow_1xMSAA";
    passName[15] = '0' + texDesc.SampleDesc.Count;

    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pImpl->m_pCurrEffectPass = pImpl->m_pEffectHelper->GetEffectPass(passName);
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_ShadowMap", input);
    pImpl->m_pCurrEffectPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_ShadowMap");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowEffect::RenderExponentialShadow(
    ID3D11DeviceContext* deviceContext, 
    ID3D11ShaderResourceView* input, 
    ID3D11RenderTargetView* output, 
    const D3D11_VIEWPORT& vp,
    float magic_power)
{
    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pImpl->m_pCurrEffectPass = pImpl->m_pEffectHelper->GetEffectPass("ExponentialShadow");
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_ShadowMap", input);
    pImpl->m_pCurrEffectPass->PSGetParamByName("c")->SetFloat(magic_power);
    pImpl->m_pCurrEffectPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_ShadowMap");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowEffect::RenderDepthToTexture(
    ID3D11DeviceContext* deviceContext,
    ID3D11ShaderResourceView* input,
    ID3D11RenderTargetView* output,
    const D3D11_VIEWPORT& vp)
{
    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pImpl->m_pCurrEffectPass = pImpl->m_pEffectHelper->GetEffectPass("Debug");
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_TextureShadow", input);
    pImpl->m_pCurrEffectPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_TextureShadow");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowEffect::VarianceShadowHorizontialBlur(
    ID3D11DeviceContext* deviceContext, 
    ID3D11ShaderResourceView* input, 
    ID3D11RenderTargetView* output, 
    const D3D11_VIEWPORT& vp, 
    int kernel_size)
{
    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    std::string passName = "VSMHorizontialBlur_" + std::to_string(kernel_size);
    auto pPass = pImpl->m_pEffectHelper->GetEffectPass(passName);
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_TextureShadow", input);
    pPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_TextureShadow");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowEffect::VarianceShadowVerticalBlur(
    ID3D11DeviceContext* deviceContext, 
    ID3D11ShaderResourceView* input, 
    ID3D11RenderTargetView* output, 
    const D3D11_VIEWPORT& vp, 
    int kernel_size)
{
    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    std::string passName = "VSMVerticalBlur_" + std::to_string(kernel_size);
    auto pPass = pImpl->m_pEffectHelper->GetEffectPass(passName);
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_TextureShadow", input);
    pPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_TextureShadow");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowEffect::ExponentialShadowLogGaussianBlur(
    ID3D11DeviceContext* deviceContext, 
    ID3D11ShaderResourceView* input, 
    ID3D11RenderTargetView* output, 
    const D3D11_VIEWPORT& vp, 
    int kernel_size, 
    float sigma)
{
    float weights[16]{};
    float twoSigmaSq = 2.0f * sigma * sigma;
    int radius = kernel_size / 2;
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        float x = (float)i;

        weights[radius + i] = expf(-x * x / twoSigmaSq);

        sum += weights[radius + i];
    }

    // 标准化权值使得权值和为1.0
    for (int i = 0; i <= kernel_size; ++i)
    {
        weights[i] /= sum;
    }

    deviceContext->IASetInputLayout(nullptr);
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    std::string passName = "ESMLogGaussianBlur_" + std::to_string(kernel_size);
    auto pPass = pImpl->m_pEffectHelper->GetEffectPass(passName);
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_TextureShadow", input);
    pImpl->m_pEffectHelper->GetConstantBufferVariable("g_BlurWeightsArray")->SetRaw(weights);
    pPass->Apply(deviceContext);
    deviceContext->OMSetRenderTargets(1, &output, nullptr);
    deviceContext->RSSetViewports(1, &vp);
    deviceContext->Draw(3, 0);

    int slot = pImpl->m_pEffectHelper->MapShaderResourceSlot("g_TextureShadow");
    input = nullptr;
    deviceContext->PSSetShaderResources(slot, 1, &input);
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}

void XM_CALLCONV ShadowEffect::SetWorldMatrix(DirectX::FXMMATRIX W)
{
    XMStoreFloat4x4(&pImpl->m_World, W);
}

void XM_CALLCONV ShadowEffect::SetViewMatrix(DirectX::FXMMATRIX V)
{
    XMStoreFloat4x4(&pImpl->m_View, V);
}

void XM_CALLCONV ShadowEffect::SetProjMatrix(DirectX::FXMMATRIX P)
{
    XMStoreFloat4x4(&pImpl->m_Proj, P);
}

void ShadowEffect::SetMaterial(const Material& material)
{
    TextureManager& tm = TextureManager::Get();

    const std::string& str = material.GetTexture("$Diffuse");
    pImpl->m_pEffectHelper->SetShaderResourceByName("g_DiffuseMap", tm.GetTexture(str));
}

MeshDataInput ShadowEffect::GetInputData(const MeshData& meshData)
{
    MeshDataInput input;
    input.pVertexBuffers = {
        meshData.m_pVertices.Get(),
        meshData.m_pNormals.Get(),
        meshData.m_pTexcoordArrays.empty() ? nullptr : meshData.m_pTexcoordArrays[0].Get()
    };
    input.strides = { 12, 12, 8 };
    input.offsets = { 0, 0, 0 };

    input.pIndexBuffer = meshData.m_pIndices.Get();
    input.indexCount = meshData.m_IndexCount;

    return input;
}

void ShadowEffect::Apply(ID3D11DeviceContext* deviceContext)
{
    XMMATRIX WVP = XMLoadFloat4x4(&pImpl->m_World) * XMLoadFloat4x4(&pImpl->m_View) * XMLoadFloat4x4(&pImpl->m_Proj);
    WVP = XMMatrixTranspose(WVP);
    pImpl->m_pEffectHelper->GetConstantBufferVariable("g_WorldViewProj")->SetFloatMatrix(4, 4, (const FLOAT*)&WVP);

    pImpl->m_pCurrEffectPass->Apply(deviceContext);
}

