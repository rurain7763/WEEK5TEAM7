#pragma once

#include "PrimitiveComponent.h"
#include "UStaticMesh.h"

class UStaticMeshComponent : public UPrimitiveComponent
{
	REFLECT_CLASS(UStaticMeshComponent, UPrimitiveComponent)

public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent() = default;

	using UPrimitiveComponent::Initialize;

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	virtual void Render(FRenderCollector& RenderCollector) override;
	virtual void Tick(float DeltaTime) override;

	inline uint32 GetLODIndex() const { return mLODIndex; }

	FAABB GetBoundingBox() const override;
    uint32 GetLODForView(const FVector& ViewOrigin) const;
	// 기존 Picking의 가상 함수 호출을 메시 에셋의 로컬 Octree로 연결합니다.
	bool RayCastComponent(const FPickingRay& PickingRay, float& OutHitT, float MaxHitT = 1.0f) const override;

	const TArray<FVertex>& GetMeshVertices() const override
	{
		if (mMeshAsset)
		{
			return mMeshAsset->GetVertices();
		}

		return UPrimitiveComponent::GetMeshVertices();
	}

	const TArray<uint32>& GetMeshIndices() const override
	{
		if (mMeshAsset)
		{
			return mMeshAsset->GetIndices();
		}

		return UPrimitiveComponent::GetMeshIndices();
	}

	// 어떤 Material을 쓰게 할 것인지 Setter
	void SetMaterial(int32 index, const TSharedPtr<FMaterialAsset>& InMaterial) 
	{ 
		mMaterialAssets[index] = InMaterial; 
		MarkRenderDirty();
	}

	// 어떤 Material을 쓰고 있는지 Getter
	const TSharedPtr<FMaterialAsset>& GetMaterial(int32 index) const { return mMaterialAssets[index]; }
	inline const TArray<TSharedPtr<FMaterialAsset>>& GetMaterials() const { return mMaterialAssets; }

	void SetColor(const FVector4& InColor) 
	{ 
		Color = InColor; 
		MarkRenderDirty();
	}

	const FVector4& GetColor() const { return Color; }

	// LOD만 바꾸는 경우 머티리얼/UV와 공통 경계를 유지하고 프록시 갱신만 요청합니다.
	void SetMesh(const TSharedPtr<FStaticMeshAsset>& InMesh, uint32 LODIndex = 0);
	inline TSharedPtr<FStaticMeshAsset> GetMesh() { return mMeshAsset; }

	FVector2 GetUVOffset(int32 index) const { return mUVOffsets[index]; }
	
	void SetUVOffset(int32 index, const FVector2& InUVOffset) 
	{ 
		mUVOffsets[index] = InUVOffset; 
		MarkRenderDirty();
	}

protected:
	virtual void OnTransformChanged() override
	{
		mbAABBDirty = true;
		Super::OnTransformChanged();
	}

private:
	FVector4 Color = FVector4(1.f, 1.f, 1.f, 1.f);
	TSharedPtr<FStaticMeshAsset> mMeshAsset;
	uint32 mLODIndex = 0;
	uint32 mRenderedLODIndex = 0;
	uint32 mLODMeshID = 0;
	TArray<TSharedPtr<FMaterialAsset>> mMaterialAssets;
	TSharedPtr<FTexture2DAsset> mTextureAsset;
	TArray<FVector2> mUVOffsets;

	mutable FAABB mCachedWorldAABB;
	mutable uint32 mCachedTransformVersion = 0;
	mutable bool mbAABBDirty = true;
};
