#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"
#include "TActiveTickList.h"

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
    // 등록되었거나 Tickable 플래그가 변경된 컴포넌트만 활성 목록에 반영합니다.
    void RefreshComponentTick(UActorComponent* Component);

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
    TArray<UActorComponent*> mUUIDRenderableComponents;
    // UUID는 표시 옵션을 순회 전에 한 번 검사하기 위해 별도의 Tick 목록에 둡니다.
    TActiveTickList<UActorComponent> mTickableComponents;
    TActiveTickList<UActorComponent> mUUIDTickableComponents;
    // 소멸 중 가상 타입 정보가 바뀌어도 등록 당시 목록에서 제거할 수 있게 보관합니다.
    struct FComponentRegistration
    {
        UPrimitiveComponent* Primitive = nullptr;
        bool bRenderable = false;
        bool bUUID = false;
    };
    TMap<UActorComponent*, FComponentRegistration> ComponentRegistrations;
	
	bool mbBVHDirty = true;
	FBVH<UPrimitiveComponent*> mBVH;
    TArray<FBVHNode*> QueryStack;
    TArray<FBVHItemRange> VisibleRanges;
};
