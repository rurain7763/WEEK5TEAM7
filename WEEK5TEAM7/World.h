#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"

class UWorld final : public UObject
{
	REFLECT_CLASS(UWorld, UObject)

public:
	UWorld() = default;
	virtual ~UWorld();

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	void AddActor(AActor* actor);
	bool RemoveActor(uint32 uuid);

	void RegisterComponent(UActorComponent* Component);
	void UnregisterComponent(UActorComponent* component);

	void MarkBoundsDirty(UActorComponent* component);

	TArray<AActor*>& GetActors() { return mActors; }

	void Tick(float deltaTime);
	void Render(float deltaTime, FRenderCollector& outCollector);

private:
	int32 getActorIndex(uint32 actorUUID) const;

private:
	enum
	{
		DEFAULT_RESERVE_MEM = 1024U
	};
	
	// Todo: Must reserve
	TArray<AActor*> mActors;
	TArray<UPrimitiveComponent*> mPrimitiveComponents;
	TArray<UActorComponent*> mNonPrimitiveRenderableComponents; // Primitive는 아닌데 렌더링 기능이 있는 컴포넌트.
	
	bool mbBVHDirty = true;
	FBVH<UPrimitiveComponent*> mBVH;
};
