#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"
#include "FWorldOctreeManager.h"

class UWorld final : public UObject
{
	REFLECT_CLASS(UWorld, UObject)
public:
	UWorld() = default;
	virtual ~UWorld();

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	void AddActor(AActor* actor);
	bool RemoveActor(uint32 componentUUID);

	TArray<AActor*>& GetActors() { return mActors; }

	void Tick(float deltaTime);
	void Render(float deltaTime, FRenderCollector& outCollector);

    void MarkSpatialDirty() { OctreeManager.MarkDirty(); }
    void MarkPrimitiveSpatialDirty(UPrimitiveComponent* Primitive) { OctreeManager.MarkPrimitiveDirty(Primitive); }
    uint32 GetSpatialUpdateCount() const { return OctreeManager.GetUpdateCount(); }
    uint32 GetSpatialUpdateBatchCount() const { return OctreeManager.GetUpdateBatchCount(); }
	void QueryPickTargets(const FPickingRay& Ray, TArray<UPrimitiveComponent*>& OutTargets);
	UPrimitiveComponent* RayCastClosest(const FPickingRay& Ray);
	const FSpatialQueryStats& GetCullingStats() const { return OctreeManager.GetCullingStats(); }
	const FSpatialQueryStats& GetPickingStats() const { return OctreeManager.GetPickingStats(); }
	uint32 GetSpatialBuildCount() const { return OctreeManager.GetBuildCount(); }
	uint32 GetSpatialEntryCount() const { return OctreeManager.GetEntryCount(); }
	uint32 GetSpatialNodeCount() const { return OctreeManager.GetNodeCount(); }

private:
	int32 getActorIndex(uint32 actorUUID) const;

private:
	enum
	{
		DEFAULT_RESERVE_MEM = 1024U
	};
	
	// Todo: Must reserve
	TArray<AActor*> mActors;

	FWorldOctreeManager OctreeManager;
};
