#include "UStaticMeshComponent.h"
#include "FAssetManager.h"
#include "RenderInfo.h"
#include "ShowFlags.h"
#include "Actor.h"
#include "JsonUtil.h"
#include "EngineMathLibrary.h"
#include "FLogManager.h"
#include "FInstrumentor.h"

void UStaticMeshComponent::Initialize(const FString& InAssetPathFileName, FVector Location,
    FRotator Rotation, FVector Scale)
{
    USceneComponent::Initialize(Location, Rotation, Scale);
}

void UStaticMeshComponent::SerializeClass(json::JSON& outJson) const
{
    USceneComponent::SerializeClass(outJson);

    FGuid AssetID = mMeshAsset ? mMeshAsset->GetAssetID() : FGuid();
    outJson["Properties"]["ObjStaticMeshAsset"] = JsonUtils::ToJson(AssetID);

	TArray<FGuid> MaterialAssetIDs;
	for (int32 i = 0; i < mMaterialAssets.Num(); ++i)
	{
		const TSharedPtr<FMaterialAsset>& MaterialAsset = mMaterialAssets[i];
		FGuid MaterialAssetID = MaterialAsset ? MaterialAsset->GetAssetID() : FGuid();
		MaterialAssetIDs.Add(MaterialAssetID);
	}
	outJson["Properties"]["ObjMaterialAssets"] = JsonUtils::ToJson(MaterialAssetIDs);
	outJson["Properties"]["UVOffsets"] = JsonUtils::ToJson(mUVOffsets);
}

void UStaticMeshComponent::DeserializeClass(const json::JSON& inJson)
{
    USceneComponent::DeserializeClass(inJson);

    const json::JSON& PropertiesJson = inJson.at("Properties");

    if (!PropertiesJson.hasKey("ObjStaticMeshAsset"))
    {
        throw std::runtime_error("UStaticMeshComponent: ObjStaticMeshAsset property is required");
    }

    if (PropertiesJson.at("ObjStaticMeshAsset").JSONType() != json::JSON::Class::Object)
    {
        throw std::runtime_error("UStaticMeshComponent: ObjStaticMeshAsset property requires an object");
    }

    FGuid AssetID = JsonUtils::FromJson<FGuid>(PropertiesJson.at("ObjStaticMeshAsset"));

    if (AssetID.IsValid())
    {
        SetMesh(FAssetManager::Get().GetAssetAs<FStaticMeshAsset>(AssetID, true));
    }

	TArray<FGuid> MaterialAssetIDs;
	if (PropertiesJson.hasKey("ObjMaterialAssets"))
	{
		JsonUtils::FromJson(PropertiesJson.at("ObjMaterialAssets"), MaterialAssetIDs);
	}

	for (int32 i = 0; i < MaterialAssetIDs.Num(); ++i)
	{
		if (i >= mMaterialAssets.Num())
		{
            break;
		}

		mMaterialAssets[i] = FAssetManager::Get().GetAssetAs<FMaterialAsset>(MaterialAssetIDs[i], true);
	}

	TArray<FVector2> UVOffsets;
	if (PropertiesJson.hasKey("UVOffsets"))
	{
		JsonUtils::FromJson(PropertiesJson.at("UVOffsets"), UVOffsets);
	}

	for (int32 i = 0; i < UVOffsets.Num(); ++i)
	{
		if (i >= mUVOffsets.Num())
		{
			break;
		}

		mUVOffsets[i] = UVOffsets[i];
	}
}

void UStaticMeshComponent::Render(FRenderCollector& RenderCollector)
{
    if (!mMeshAsset)
    {
        return;
    }

    if (!FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
    {
        return;
    }


	const FTransform& Transform = GetTransform();
    const FStaticMeshAsset& Mesh = mMeshAsset->GetLODMesh(LODLevel);

    for (int32 SectionIndex = 0; SectionIndex < Mesh.GetSections().Num(); ++SectionIndex)
    {
        const FStaticMeshSection& Section = Mesh.GetSections()[SectionIndex];

        TSharedPtr<FMaterialAsset> Material = mMaterialAssets[SectionIndex];

        uint16 PipelineID = Material ? Material->GetPipelineID() : 1;
        uint32 MaterialID = Material ? Material->GetMaterialID() : 0;
        uint32 MeshID = Mesh.GetMeshID();

        FRenderInfo RenderInfo;
        RenderInfo.SortKey = MakeRenderSortKey(PipelineID, MaterialID, MeshID);
        RenderInfo.Pipeline = Material ? Material->GetPipeline() : nullptr;
        RenderInfo.VertexBuffer = Mesh.GetVertexBuffer();
        RenderInfo.IndexBuffer = Mesh.GetIndexBuffer();
        RenderInfo.StartIndex = Section.FirstIndex;
        RenderInfo.IndexCount = Section.IndexCount;
        RenderInfo.Texture = Material ? Material->GetDiffuseTexture() : nullptr;
        RenderInfo.UVOffset = mUVOffsets[SectionIndex];
        RenderInfo.Model = Transform.MakeMatrix();
        RenderInfo.Color = Material ? FVector4(Material->GetDiffuseColor().x, Material->GetDiffuseColor().y, Material->GetDiffuseColor().z, Material->GetOpacity()) : Color;
        RenderInfo.UseVertexColor = Material == nullptr;
        RenderInfo.ObjectInternalIndex = mOwner->InternalIndex;

        RenderCollector.RenderInfos.Add(RenderInfo);
    }
}

bool UStaticMeshComponent::RayCastAfterBounds(const FPickingRay& Ray, float MaxHitT, float& OutHitT) const
{
    PROFILE_SCOPE("Picking/LocalMeshOctree");
    if (!mMeshAsset) return false;
    const FMatrix& Inverse = GetTransform().InverseMatrix();
    if (Inverse == FMatrix::Zero) return false;
    // 양 끝점을 변환하면 비균일·음수 스케일에서도 구간 비율 T는 보존됩니다.
    const FPickingRay LocalRay(Inverse.TransformPosition(Ray.Near), Inverse.TransformPosition(Ray.Far));
    return mMeshAsset->GetLODMesh(LODLevel).RayCastLocal(LocalRay, OutHitT, nullptr, MaxHitT);
}

FAABB UStaticMeshComponent::GetBoundingBox() const
{
    if (!mMeshAsset)
    {
        return FAABB::Invalid();
    }

    const FTransform& Transform = GetTransform();
    const uint32 CurrentTransformVersion = Transform.GetTransformVersion();
    if (mbAABBDirty || mCachedTransformVersion != CurrentTransformVersion)
    {
        mCachedWorldAABB = mMeshAsset->GetLocalBoundingBox().ToWorld(Transform.MakeMatrix());
        mCachedTransformVersion = CurrentTransformVersion;
        mbAABBDirty = false;
    }

    return mCachedWorldAABB;
}

void UStaticMeshComponent::SetMesh(const TSharedPtr<FStaticMeshAsset>& InMesh)
{
    mbAABBDirty = true;
	MarkSpatialDirty();

    if (!InMesh)
    {
		mMeshAsset = nullptr;
		mMaterialAssets.Empty();
		mUVOffsets.Empty();
		return;
    }

    const auto& Sections = InMesh->GetSections();
    mMaterialAssets.SetNum(Sections.Num());
    mUVOffsets.SetNum(Sections.Num());
    for (int32 i = 0; i < Sections.Num(); i++)
    {
        auto& Section = Sections[i];
		mMaterialAssets[i] = Section.MaterialAssetID.IsValid()
            ? FAssetManager::Get().GetAssetAs<FMaterialAsset>(Section.MaterialAssetID, true) : nullptr;
    }
    mMeshAsset = InMesh;
}
