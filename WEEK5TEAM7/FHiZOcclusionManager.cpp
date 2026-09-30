#include "FHiZOcclusionManager.h"
#include "Renderer.h"
#include "FLogManager.h"
#include "MathUtility.h"

FHiZOcclusionManager::FHiZOcclusionManager()
{
}

FHiZOcclusionManager::~FHiZOcclusionManager()
{
	Release();
}

void FHiZOcclusionManager::Release()
{
	mCSDownsampleInit.Reset();
	mCSDownsampleMip.Reset();
	mCSCulling.Reset();
	mPointClampSampler.Reset();
	mDownsampleConstantBuffer.Reset();
	mCullConstantBuffer.Reset();

	mHiZTexture.Reset();
	mHiZCopyTexture.Reset();
	mHiZSRV.Reset();
	mMipUAVs.clear();
	mCopyMipSRVs.clear();

	mAABBBuffer.Reset();
	mAABBSRV.Reset();
	mVisibilityBuffer.Reset();
	mVisibilityUAV.Reset();

	for (uint32 i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		mStagingBuffers[i].Reset();
		mStagingBufferReady[i] = false;
	}

	mCpuVisibility.clear();
	mNumObjects = 0;
	mCulledCount = 0;
	bInitialized = false;
	bHasValidVisibility = false;
}

void FHiZOcclusionManager::Initialize(ID3D11Device* Device)
{
	if (bInitialized || !Device)
	{
		return;
	}

	CompileShaders(Device);
	EnsureHiZTextures(Device);

	// Create Point Clamp Sampler
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	Device->CreateSamplerState(&samplerDesc, mPointClampSampler.GetAddressOf());

	// Create Downsample Constant Buffer
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(FDownsampleConstants);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	Device->CreateBuffer(&cbDesc, nullptr, mDownsampleConstantBuffer.GetAddressOf());

	// Create Cull Constant Buffer
	cbDesc.ByteWidth = sizeof(FCullConstants);
	Device->CreateBuffer(&cbDesc, nullptr, mCullConstantBuffer.GetAddressOf());

	bInitialized = true;
}

void FHiZOcclusionManager::CompileShaders(ID3D11Device* Device)
{
	auto CompileCS = [Device](const wchar_t* FilePath, const char* EntryPoint, Microsoft::WRL::ComPtr<ID3D11ComputeShader>& OutCS)
	{
		ID3DBlob* ShaderBlob = nullptr;
		ID3DBlob* ErrorBlob = nullptr;
		UINT Flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
		Flags |= D3DCOMPILE_DEBUG;
#else
		Flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

		HRESULT hr = D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoint, "cs_5_0", Flags, 0, &ShaderBlob, &ErrorBlob);
		if (FAILED(hr))
		{
			if (ErrorBlob)
			{
				UE_DEBUG_LOG_ERROR("Failed to compile Compute Shader %ls: %s", FilePath, (const char*)ErrorBlob->GetBufferPointer());
				ErrorBlob->Release();
			}
			return false;
		}

		hr = Device->CreateComputeShader(ShaderBlob->GetBufferPointer(), ShaderBlob->GetBufferSize(), nullptr, OutCS.GetAddressOf());
		ShaderBlob->Release();
		return SUCCEEDED(hr);
	};

	CompileCS(L"Assets/Shaders/HiZDownsampleInit.hlsl", "CSMain", mCSDownsampleInit);
	CompileCS(L"Assets/Shaders/HiZDownsampleMip.hlsl", "CSMain", mCSDownsampleMip);
	CompileCS(L"Assets/Shaders/HiZCulling.hlsl", "CSMain", mCSCulling);
}

void FHiZOcclusionManager::EnsureHiZTextures(ID3D11Device* Device)
{
	if (mHiZTexture)
	{
		return;
	}

	mHiZWidth = 1024;
	mHiZHeight = 512;
	mNumMips = 11; // 1024x512 down to 1x1

	// 1. Destination HiZ Texture with full mip chain
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = mHiZWidth;
	texDesc.Height = mHiZHeight;
	texDesc.MipLevels = mNumMips;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	Device->CreateTexture2D(&texDesc, nullptr, mHiZTexture.GetAddressOf());

	// 2. Copy Texture used as intermediate read source during downsampling (avoids subresource hazard)
	D3D11_TEXTURE2D_DESC copyDesc = texDesc;
	copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Device->CreateTexture2D(&copyDesc, nullptr, mHiZCopyTexture.GetAddressOf());

	// 3. SRV for the entire HiZ texture mip chain (used in culling shader)
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = mNumMips;
	Device->CreateShaderResourceView(mHiZTexture.Get(), &srvDesc, mHiZSRV.GetAddressOf());

	// 4. UAVs for each mip of mHiZTexture
	mMipUAVs.resize(mNumMips);
	for (uint32 i = 0; i < mNumMips; ++i)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = i;
		Device->CreateUnorderedAccessView(mHiZTexture.Get(), &uavDesc, mMipUAVs[i].GetAddressOf());
	}

	// 5. Individual mip SRVs for mHiZCopyTexture (used to downsample mip k from mip k-1)
	mCopyMipSRVs.resize(mNumMips);
	for (uint32 i = 0; i < mNumMips; ++i)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {};
		mipSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		mipSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		mipSrvDesc.Texture2D.MostDetailedMip = i;
		mipSrvDesc.Texture2D.MipLevels = 1;
		Device->CreateShaderResourceView(mHiZCopyTexture.Get(), &mipSrvDesc, mCopyMipSRVs[i].GetAddressOf());
	}
}

void FHiZOcclusionManager::EnsureBuffers(ID3D11Device* Device, uint32 Count)
{
	if (Count == 0)
	{
		mNumObjects = 0;
		return;
	}

	if (Count == mNumObjects && mAABBBuffer && mVisibilityBuffer)
	{
		return;
	}

	mNumObjects = Count;
	bHasValidVisibility = false;

	// Reset existing buffers
	mAABBBuffer.Reset();
	mAABBSRV.Reset();
	mVisibilityBuffer.Reset();
	mVisibilityUAV.Reset();
	for (uint32 i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		mStagingBuffers[i].Reset();
		mStagingBufferReady[i] = false;
	}

	// 1. AABB StructuredBuffer
	D3D11_BUFFER_DESC bufDesc = {};
	bufDesc.ByteWidth = sizeof(FAABB) * Count;
	bufDesc.Usage = D3D11_USAGE_DEFAULT;
	bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufDesc.StructureByteStride = sizeof(FAABB);
	Device->CreateBuffer(&bufDesc, nullptr, mAABBBuffer.GetAddressOf());

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = Count;
	Device->CreateShaderResourceView(mAABBBuffer.Get(), &srvDesc, mAABBSRV.GetAddressOf());

	// 2. Visibility RWStructuredBuffer
	D3D11_BUFFER_DESC visDesc = {};
	visDesc.ByteWidth = sizeof(uint32) * Count;
	visDesc.Usage = D3D11_USAGE_DEFAULT;
	visDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	visDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	visDesc.StructureByteStride = sizeof(uint32);
	Device->CreateBuffer(&visDesc, nullptr, mVisibilityBuffer.GetAddressOf());

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = Count;
	Device->CreateUnorderedAccessView(mVisibilityBuffer.Get(), &uavDesc, mVisibilityUAV.GetAddressOf());

	// 3. Staging Buffers for CPU Readback
	D3D11_BUFFER_DESC stagingDesc = {};
	stagingDesc.ByteWidth = sizeof(uint32) * Count;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	for (uint32 i = 0; i < NUM_STAGING_BUFFERS; ++i)
	{
		Device->CreateBuffer(&stagingDesc, nullptr, mStagingBuffers[i].GetAddressOf());
		mStagingBufferReady[i] = false;
	}

	mCpuVisibility.assign(Count, 1); // Default all visible
}

void FHiZOcclusionManager::UpdateAABBs(ID3D11Device* Device, ID3D11DeviceContext* Context, const TArray<FAABB>& InAABBs)
{
	if (!Device || !Context)
	{
		return;
	}

	Initialize(Device);

	uint32 Count = static_cast<uint32>(InAABBs.Num());
	EnsureBuffers(Device, Count);

	if (Count > 0 && mAABBBuffer)
	{
		Context->UpdateSubresource(mAABBBuffer.Get(), 0, nullptr, InAABBs.Data(), 0, 0);
	}
}

void FHiZOcclusionManager::BeginFrame(ID3D11DeviceContext* Context)
{
	mCulledCount = 0;

	if (!bEnabled || !bInitialized || mNumObjects == 0 || !Context)
	{
		return;
	}

	// Read from previous completed staging buffer
	uint32 readIndex = (mWriteIndex + 1) % NUM_STAGING_BUFFERS;
	if (mStagingBufferReady[readIndex] && mStagingBuffers[readIndex])
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = Context->Map(mStagingBuffers[readIndex].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (SUCCEEDED(hr))
		{
			const uint32* pData = static_cast<const uint32*>(mapped.pData);
			if (mCpuVisibility.size() != mNumObjects)
			{
				mCpuVisibility.resize(mNumObjects);
			}
			std::memcpy(mCpuVisibility.data(), pData, mNumObjects * sizeof(uint32));
			Context->Unmap(mStagingBuffers[readIndex].Get(), 0);
			bHasValidVisibility = true;
		}
	}
}

void FHiZOcclusionManager::GenerateHiZAndDispatchCull(
	URenderer* Renderer,
	const TSharedPtr<FDepthStencil>& SceneDepthStencil,
	const FMatrix& ViewProjectionMatrix
)
{
	if (!bEnabled || !Renderer || !SceneDepthStencil || !SceneDepthStencil->DepthSRV || mNumObjects == 0)
	{
		return;
	}

	ID3D11Device* Device = Renderer->GetDevice();
	ID3D11DeviceContext* Context = Renderer->GetDeviceContext();
	if (!Device || !Context)
	{
		return;
	}

	Initialize(Device);

	if (!mCSDownsampleInit || !mCSDownsampleMip || !mCSCulling)
	{
		return;
	}

	// 1. Temporarily unbind DSV from Output Merger to prevent SRV read hazard
	ID3D11RenderTargetView* CurrentRTV = Renderer->GetBindedRenderTarget() ? Renderer->GetBindedRenderTarget()->RTV.Get() : nullptr;
	Context->OMSetRenderTargets(1, &CurrentRTV, nullptr);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;

	// 2. Generate Mip 0 from Scene DepthStencil SRV
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(Context->Map(mDownsampleConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			FDownsampleConstants* cb = static_cast<FDownsampleConstants*>(mapped.pData);
			cb->DestSize[0] = mHiZWidth;
			cb->DestSize[1] = mHiZHeight;
			cb->SrcSize[0] = SceneDepthStencil->Width;
			cb->SrcSize[1] = SceneDepthStencil->Height;
			Context->Unmap(mDownsampleConstantBuffer.Get(), 0);
		}

		Context->CSSetShader(mCSDownsampleInit.Get(), nullptr, 0);
		Context->CSSetConstantBuffers(0, 1, mDownsampleConstantBuffer.GetAddressOf());
		Context->CSSetShaderResources(0, 1, SceneDepthStencil->DepthSRV.GetAddressOf());
		Context->CSSetUnorderedAccessViews(0, 1, mMipUAVs[0].GetAddressOf(), nullptr);

		Context->Dispatch((mHiZWidth + 15) / 16, (mHiZHeight + 15) / 16, 1);

		Context->CSSetShaderResources(0, 1, &nullSRV);
		Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		// Copy Mip 0 to mHiZCopyTexture
		Context->CopySubresourceRegion(mHiZCopyTexture.Get(), 0, 0, 0, 0, mHiZTexture.Get(), 0, nullptr);
	}

	// 3. Hierarchically downsample mips 1..10
	{
		Context->CSSetShader(mCSDownsampleMip.Get(), nullptr, 0);
		Context->CSSetConstantBuffers(0, 1, mDownsampleConstantBuffer.GetAddressOf());

		for (uint32 mip = 1; mip < mNumMips; ++mip)
		{
			uint32 curW = FMath::Max(1u, mHiZWidth >> mip);
			uint32 curH = FMath::Max(1u, mHiZHeight >> mip);
			uint32 prevW = FMath::Max(1u, mHiZWidth >> (mip - 1));
			uint32 prevH = FMath::Max(1u, mHiZHeight >> (mip - 1));

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(Context->Map(mDownsampleConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				FDownsampleConstants* cb = static_cast<FDownsampleConstants*>(mapped.pData);
				cb->DestSize[0] = curW;
				cb->DestSize[1] = curH;
				cb->SrcSize[0] = prevW;
				cb->SrcSize[1] = prevH;
				Context->Unmap(mDownsampleConstantBuffer.Get(), 0);
			}

			Context->CSSetShaderResources(0, 1, mCopyMipSRVs[mip - 1].GetAddressOf());
			Context->CSSetUnorderedAccessViews(0, 1, mMipUAVs[mip].GetAddressOf(), nullptr);

			Context->Dispatch((curW + 15) / 16, (curH + 15) / 16, 1);

			Context->CSSetShaderResources(0, 1, &nullSRV);
			Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

			// Copy current mip to mHiZCopyTexture
			Context->CopySubresourceRegion(mHiZCopyTexture.Get(), mip, 0, 0, 0, mHiZTexture.Get(), mip, nullptr);
		}
	}

	// 4. Dispatch Culling Compute Shader
	if (mNumObjects > 0 && mAABBSRV && mVisibilityUAV)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(Context->Map(mCullConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			FCullConstants* cb = static_cast<FCullConstants*>(mapped.pData);
			cb->ViewProjection = ViewProjectionMatrix;
			cb->HiZResolution[0] = static_cast<float>(mHiZWidth);
			cb->HiZResolution[1] = static_cast<float>(mHiZHeight);
			cb->NumObjects = mNumObjects;
			cb->MaxMipLevel = mNumMips - 1;
			cb->DepthBias = mDepthBias;
			Context->Unmap(mCullConstantBuffer.Get(), 0);
		}

		Context->CSSetShader(mCSCulling.Get(), nullptr, 0);
		Context->CSSetConstantBuffers(0, 1, mCullConstantBuffer.GetAddressOf());
		Context->CSSetSamplers(0, 1, mPointClampSampler.GetAddressOf());

		ID3D11ShaderResourceView* srvs[2] = { mAABBSRV.Get(), mHiZSRV.Get() };
		Context->CSSetShaderResources(0, 2, srvs);
		Context->CSSetUnorderedAccessViews(0, 1, mVisibilityUAV.GetAddressOf(), nullptr);

		uint32 groupCountX = (mNumObjects + 63) / 64;
		Context->Dispatch(groupCountX, 1, 1);

		ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
		Context->CSSetShaderResources(0, 2, nullSRVs);
		Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

		// 5. Copy result to CPU staging buffer (pipelined)
		mWriteIndex = (mWriteIndex + 1) % NUM_STAGING_BUFFERS;
		if (mStagingBuffers[mWriteIndex])
		{
			Context->CopyResource(mStagingBuffers[mWriteIndex].Get(), mVisibilityBuffer.Get());
			mStagingBufferReady[mWriteIndex] = true;
		}
	}

	// 6. Restore DepthStencil to OM
	Context->OMSetRenderTargets(1, &CurrentRTV, SceneDepthStencil->DSV.Get());
}
