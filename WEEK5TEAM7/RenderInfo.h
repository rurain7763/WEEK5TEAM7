#pragma once

#include "Transform.h"
#include "Object.h"
#include "FName.h"
#include "Assets.h"
#include "TArray.h"
#include "FFrustum.h"
#include "FBVH.h"
#include "TRangePool.h"
#include <algorithm>

class FCamera;
class UPrimitiveComponent;
class FRenderPipeline;

inline uint64 MakeRenderSortKey(uint16 pipelineID, uint32 materialID, uint32 meshID)
{
	return (static_cast<uint64>(pipelineID) << 48) |
		((static_cast<uint64>(materialID) & 0x00FFFFFF) << 24) |
		(static_cast<uint64>(meshID) & 0x00FFFFFF);
}

enum class ERenderBlendMode
{
	Opaque,
	Masked,
	Transparent,
	Additive,
	Count
};

struct FRenderInfo
{
	uint64 SortKey = 0;
	TSharedPtr<FRenderPipeline> Pipeline;
	// 버퍼 소유권은 메시 에셋 또는 GraphicsManager에 있습니다.
	// 수집부터 Draw 제출까지 버퍼를 교체/해제하지 않고, 다음 프레임에는 다시 수집합니다.
	ID3D11Buffer* VertexBuffer = nullptr;
	uint32 VertexCount = 0;
	ID3D11Buffer* IndexBuffer = nullptr;
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	TSharedPtr<FTexture2DAsset> Texture;
	FVector2 UVOffset = { 0.f, 0.f };
	FMatrix Model;
	uint32 ObjectInternalIndex;
	FVector4 Color = { 1.f, 1.f, 1.f, 1.f };
	bool UseVertexColor = true;

	bool operator<(const FRenderInfo& Other) const
	{
		return SortKey < Other.SortKey;
	}
};

struct FRenderQuadInfo
{
	FMatrix Model;
	FVector4 Color = { 1.f, 1.f, 1.f, 1.f };
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> TextureSRV;
	FVector4 SubUV = { 0.f, 0.f, 1.f, 1.f };
	ERenderBlendMode BlendMode = ERenderBlendMode::Opaque;
	bool EnableDepthTest = true;
	bool EnableDepthWrite = true;
};

struct FRenderQuad2DInfo
{
	FVector2 Position;
	FVector2 Size;
	FVector4 Color = { 1.f, 1.f, 1.f, 1.f };
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> TextureSRV;
	FVector4 SubUV = { 0.f, 0.f, 1.f, 1.f };
	float Rotation = 0.f;
	ERenderBlendMode BlendMode = ERenderBlendMode::Opaque;
};

struct FRenderLineInfo
{
	FVector4 Color;
	FVector3 Start;
	float Thickness;
	FVector3 End;
	float Padding;
};

// 이번 프레임에 그릴 것들을 한데 모은다. 소유자는 FGraphicsManager.
struct FRenderCollector
{
public:
	enum { DEFAULT_RESERVE_MEM = 1024U };

	FCamera* Camera = nullptr;
	FFrustum Frustum;
	bool bNeedPickTargets = false;

	TArray<FRenderLineInfo> LineInfos; // 라인 패스
	FBVH<UPrimitiveComponent*>* BVH = nullptr;

	inline void AddQuadInfo(const FRenderQuadInfo& QuadInfo)
	{
		if (QuadInfo.EnableDepthTest)
		{
			if (QuadInfo.EnableDepthWrite)
			{
				OpaqueQuadInfos.Add(QuadInfo);
			}
			else
			{
				TransparentQuadInfos.Add(QuadInfo);
			}
		}
		else
		{
			OverlayQuadInfos.Add(QuadInfo);
		}
	}

	inline void AddQuad2DInfo(const FRenderQuad2DInfo& Quad2DInfo)
	{
		Quad2DInfos.Add(Quad2DInfo);
	}

	inline void Clear()
	{
		BVH = nullptr;
		bNeedPickTargets = false;
		VisibleRenderInfoIndices.Reset(DEFAULT_RESERVE_MEM);
		LineInfos.Reset(DEFAULT_RESERVE_MEM);
		OpaqueQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		TransparentQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		OverlayQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		Quad2DInfos.Reset(DEFAULT_RESERVE_MEM);
	}

	inline const TArray<FRenderQuadInfo>& GetOpaqueQuadInfos() const { return OpaqueQuadInfos; }
	inline const TArray<FRenderQuadInfo>& GetTransparentQuadInfos() const { return TransparentQuadInfos; }
	inline const TArray<FRenderQuadInfo>& GetOverlayQuadInfos() const { return OverlayQuadInfos; }
	inline const TArray<FRenderQuad2DInfo>& GetQuad2DInfos() const { return Quad2DInfos; }
	
	inline const TRangePool<FRenderInfo>& GetRenderInfoPool() const { return RenderInfoPool; }
	inline TArray<int32>& GetVisibleRenderInfoIndices() { return VisibleRenderInfoIndices; }

private:
	friend class FRenderProxy;

	TRangePool<FRenderInfo> RenderInfoPool;
	TArray<int32> VisibleRenderInfoIndices;

	TArray<FRenderQuadInfo> OpaqueQuadInfos;
	TArray<FRenderQuadInfo> TransparentQuadInfos;
	TArray<FRenderQuadInfo> OverlayQuadInfos;

	TArray<FRenderQuad2DInfo> Quad2DInfos;
};

class FRenderProxy
{
public:
	FRenderProxy() = default;
	~FRenderProxy()
	{
		ReleaseRenderInfos();
	}

	inline void SetCollector(FRenderCollector& InCollector)
	{
		if (Collector == &InCollector)
		{
			return;
		}

		ReleaseRenderInfos();
		Collector = &InCollector;
	}

	inline void ReserveRenderInfos(int32 Count)
	{
		if (RenderInfoBlock.IsValid())
		{
			if (RenderInfoBlock.Size >= Count)
			{
				return;
			}

			Collector->RenderInfoPool.Release(RenderInfoBlock);
		}

		RenderInfoBlock = Collector->RenderInfoPool.Allocate(Count);
		ActiveRenderInfoNum = 0;
	}

	inline void ReleaseRenderInfos()
	{
		if (!RenderInfoBlock.IsValid())
		{
			return;
		}

		Collector->RenderInfoPool.Release(RenderInfoBlock);
		RenderInfoBlock = {};
		ActiveRenderInfoNum = 0;
	}

	inline void SetActiveRenderInfoNum(int32 Count)
	{
		ActiveRenderInfoNum = Count;
	}

	inline FRenderInfo& GetRenderInfo(uint32 Index)
	{
		return Collector->RenderInfoPool.Get(RenderInfoBlock, Index);
	}

	inline void Submit()
	{
		if (!RenderInfoBlock.IsValid())
		{
			return;
		}

		for (uint32 i = 0; i < ActiveRenderInfoNum; ++i)
		{
			Collector->VisibleRenderInfoIndices.Emplace(RenderInfoBlock.Index + i);
		}
	}

private:
	FRenderCollector* Collector = nullptr;

	FRangePoolBlock RenderInfoBlock;
	uint32 ActiveRenderInfoNum = 0;
};
