#pragma once

#include "Core.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "Matrix.h"
#include "Vector.h"
#include "FAABB.h"
#include "TArray.h"
#include <vector>

class URenderer;
struct FDepthStencil;

class FHiZOcclusionManager
{
public:
	static FHiZOcclusionManager& Get()
	{
		static FHiZOcclusionManager Instance;
		return Instance;
	}

	FHiZOcclusionManager();
	~FHiZOcclusionManager();

	void Initialize(ID3D11Device* Device);
	void Release();

	// BVH AABBs updated
	void UpdateAABBs(ID3D11Device* Device, ID3D11DeviceContext* Context, const TArray<FAABB>& InAABBs);

	// Called after opaque mesh pass in GraphicsManager::Render
	void GenerateHiZAndDispatchCull(
		URenderer* Renderer,
		const TSharedPtr<FDepthStencil>& SceneDepthStencil,
		const FMatrix& ViewProjectionMatrix
	);

	// Called at beginning of UWorld::Render to read back visibility
	void BeginFrame(ID3D11DeviceContext* Context);

	// Fast O(1) visibility query during CollectPrimitives
	inline bool IsOccluded(int32 Index) const
	{
		if (!bEnabled || !bHasValidVisibility || Index < 0 || Index >= static_cast<int32>(mCpuVisibility.size()))
		{
			return false; // Not occluded (render it)
		}
		return mCpuVisibility[Index] == 0;
	}

	inline void IncrementCulledCount() { mCulledCount++; }
	inline uint32 GetCulledCount() const { return mCulledCount; }
	inline uint32 GetTotalTestedCount() const { return mNumObjects; }

	inline bool IsEnabled() const { return bEnabled; }
	inline void SetEnabled(bool InEnabled) { bEnabled = InEnabled; }

	inline float GetDepthBias() const { return mDepthBias; }
	inline void SetDepthBias(float InBias) { mDepthBias = InBias; }

private:
	void EnsureHiZTextures(ID3D11Device* Device);
	void CompileShaders(ID3D11Device* Device);
	void EnsureBuffers(ID3D11Device* Device, uint32 Count);

private:
	bool bInitialized = false;
	bool bEnabled = true;
	bool bHasValidVisibility = false;

	float mDepthBias = 0.0001f;

	uint32 mHiZWidth = 1024;
	uint32 mHiZHeight = 512;
	uint32 mNumMips = 11;

	uint32 mNumObjects = 0;
	uint32 mCulledCount = 0;

	// Shaders
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> mCSDownsampleInit;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> mCSDownsampleMip;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> mCSCulling;

	// Sampler
	Microsoft::WRL::ComPtr<ID3D11SamplerState> mPointClampSampler;

	// Constant buffers
	struct FDownsampleConstants
	{
		uint32 DestSize[2];
		uint32 SrcSize[2];
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> mDownsampleConstantBuffer;

	struct FCullConstants
	{
		FMatrix ViewProjection;
		float HiZResolution[2];
		uint32 NumObjects;
		uint32 MaxMipLevel;
		float DepthBias;
		float Padding[3];
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> mCullConstantBuffer;

	// Hi-Z Texture and Views
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mHiZTexture;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mHiZCopyTexture; // Scratch copy to eliminate subresource hazards
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mHiZSRV; // Whole mip chain SRV for culling
	std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> mMipUAVs;
	std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> mCopyMipSRVs; // Individual mip SRVs for downsampling

	// Structured Buffers
	Microsoft::WRL::ComPtr<ID3D11Buffer> mAABBBuffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mAABBSRV;

	Microsoft::WRL::ComPtr<ID3D11Buffer> mVisibilityBuffer;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> mVisibilityUAV;

	// Staging Buffers for zero-stall readback (Double-buffered)
	static constexpr uint32 NUM_STAGING_BUFFERS = 2;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mStagingBuffers[NUM_STAGING_BUFFERS];
	bool mStagingBufferReady[NUM_STAGING_BUFFERS] = { false, false };
	uint32 mWriteIndex = 0;

	// CPU side cached visibility array
	std::vector<uint32> mCpuVisibility;
};
