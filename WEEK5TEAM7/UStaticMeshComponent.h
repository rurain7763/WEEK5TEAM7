#pragma once

#include "PrimitiveComponent.h"
#include "UStaticMesh.h"

class UStaticMeshComponent : public UPrimitiveComponent
{
	REFLECT_CLASS(UStaticMeshComponent, UPrimitiveComponent)

public:
	UStaticMeshComponent() = default;

	using UPrimitiveComponent::Initialize;
	void Initialize(const FString& InAssetPathFileName, FVector Location, FRotator Rotation, FVector Scale);

	virtual ~UStaticMeshComponent() = default;

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	virtual void Render(FRenderCollector& RenderCollector) override;

	FAABB GetBoundingBox() const override;
    bool RayCastAfterBounds(const FPickingRay& PickingRay, float MaxHitT, float& OutHitT) const override;
    // 거리 정책 없이 수동으로 선택합니다. 렌더링과 Picking이 같은 메시를 사용합니다.
    void SetLODLevel(uint32 Level) { LODLevel = Level < 3 ? Level : 0; }
    uint32 GetLODLevel() const { return LODLevel; }

	const TArray<FVertex>& GetMeshVertices() const override
	{
		if (mMeshAsset)
		{
			return mMeshAsset->GetLODMesh(LODLevel).GetVertices();
		}

		return UPrimitiveComponent::GetMeshVertices();
	}

	const TArray<uint32>& GetMeshIndices() const override
	{
		if (mMeshAsset)
		{
			return mMeshAsset->GetLODMesh(LODLevel).GetIndices();
		}

		return UPrimitiveComponent::GetMeshIndices();
	}

	// 어떤 Material을 쓰게 할 것인지 Setter
	void SetMaterial(int32 index, const TSharedPtr<FMaterialAsset>& InMaterial) { mMaterialAssets[index] = InMaterial; }

	// 어떤 Material을 쓰고 있는지 Getter
	const TSharedPtr<FMaterialAsset>& GetMaterial(int32 index) const { return mMaterialAssets[index]; }
	inline const TArray<TSharedPtr<FMaterialAsset>>& GetMaterials() const { return mMaterialAssets; }

	void SetColor(const FVector4& InColor) { Color = InColor; }
	const FVector4& GetColor() const { return Color; }

	void SetMesh(const TSharedPtr<FStaticMeshAsset>& InMesh);
	inline TSharedPtr<FStaticMeshAsset> GetMesh() { return mMeshAsset; }

	FVector2 GetUVOffset(int32 index) const { return mUVOffsets[index]; }
	void SetUVOffset(int32 index, const FVector2& InUVOffset) { mUVOffsets[index] = InUVOffset; }

private:
    uint32 LODLevel = 0;
	FVector4 Color = FVector4(1.f, 1.f, 1.f, 1.f);
	TSharedPtr<FStaticMeshAsset> mMeshAsset;
	TArray<TSharedPtr<FMaterialAsset>> mMaterialAssets;
	TSharedPtr<FTexture2DAsset> mTextureAsset;
	TArray<FVector2> mUVOffsets;

	mutable FAABB mCachedWorldAABB;
	mutable uint32 mCachedTransformVersion = 0;
	mutable bool mbAABBDirty = true;
};
