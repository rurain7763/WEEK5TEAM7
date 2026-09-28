#pragma once

#include "Transform.h"
#include "Object.h"
#include "FName.h"
#include "Assets.h"
#include "TArray.h"
#include "FFrustum.h"
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
	Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
	uint32 VertexCount = 0;
	Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;
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
	bool bEnableFrustumCulling = true;
	bool bNeedPickTargets = false;
	uint32 TotalMeshCount = 0;
	uint32 CulledMeshCount = 0;

	TArray<FRenderInfo> RenderInfos; // 메시 패스
	TArray<FRenderLineInfo> LineInfos; // 라인 패스
	TArray<UPrimitiveComponent*> PickTargets;

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

	// Sort the RenderInfos based on Texture, VertexBuffer, and IndexBuffer to minimize state changes during rendering.
	inline void Sort()
	{
		std::sort(RenderInfos.begin(), RenderInfos.end(), [](const FRenderInfo& A, const FRenderInfo& B) {
			const auto TextureA = A.Texture ? A.Texture->GetSRV() : nullptr;
			const auto TextureB = B.Texture ? B.Texture->GetSRV() : nullptr;

			if (TextureA != TextureB)
			{
				return TextureA < TextureB;
			}

			const auto VBA = A.VertexBuffer.Get();
			const auto VBB = B.VertexBuffer.Get();

			if (VBA != VBB)
			{
				return VBA < VBB;
			}

			return A.IndexBuffer.Get() < B.IndexBuffer.Get();
		});
	}

	inline void Clear()
	{
		TotalMeshCount = 0;
		CulledMeshCount = 0;
		bNeedPickTargets = false;
		RenderInfos.Reset(DEFAULT_RESERVE_MEM);
		LineInfos.Reset(DEFAULT_RESERVE_MEM);
		PickTargets.Reset(DEFAULT_RESERVE_MEM);
		OpaqueQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		TransparentQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		OverlayQuadInfos.Reset(DEFAULT_RESERVE_MEM);
		Quad2DInfos.Reset(DEFAULT_RESERVE_MEM);
	}

	inline const TArray<FRenderQuadInfo>& GetOpaqueQuadInfos() const { return OpaqueQuadInfos; }
	inline const TArray<FRenderQuadInfo>& GetTransparentQuadInfos() const { return TransparentQuadInfos; }
	inline const TArray<FRenderQuadInfo>& GetOverlayQuadInfos() const { return OverlayQuadInfos; }
	inline const TArray<FRenderQuad2DInfo>& GetQuad2DInfos() const { return Quad2DInfos; }

private:
	TArray<FRenderQuadInfo> OpaqueQuadInfos;
	TArray<FRenderQuadInfo> TransparentQuadInfos;
	TArray<FRenderQuadInfo> OverlayQuadInfos;

	TArray<FRenderQuad2DInfo> Quad2DInfos;
};