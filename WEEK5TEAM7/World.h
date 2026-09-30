#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"
#include "TMap.h"

class UWorld final : public UObject
{
	REFLECT_CLASS(UWorld, UObject)

public:
	UWorld();
	virtual ~UWorld();

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	void AddActor(AActor* actor);
	bool RemoveActor(uint32 uuid);

	void RegisterComponent(UActorComponent* Component);
	void UnregisterComponent(UActorComponent* component);
	// Tickable 변경 시에만 활성 목록을 갱신합니다.
	void RefreshComponentTick(UActorComponent* Component);

	void MarkBoundsDirty(UActorComponent* component);

	TArray<AActor*>& GetActors() { return mActors; }

	void Tick(float deltaTime);
	void Render(float deltaTime, FRenderCollector& outCollector);

private:
	int32 getActorIndex(uint32 actorUUID) const;
    // 표시 옵션이 바뀐 경우에만 UUID의 Tick 등록을 다시 계산합니다.
    void RefreshUUIDTickVisibility();

	struct FComponentTickList
	{
		void Add(UActorComponent* Component);
		void Remove(UActorComponent* Component);
		void Tick(float DeltaTime, int32 Count);

		TArray<UActorComponent*> Components;
		// 등록/해제할 때만 사용하며 프레임 순회 중에는 조회하지 않습니다.
		TMap<UActorComponent*, uint32> Indices;
		bool bTicking = false;
		bool bNeedsCompaction = false;
	};

private:
	enum
	{
		DEFAULT_RESERVE_MEM = 1024U
	};
	
	// Todo: Must reserve
	TArray<AActor*> mActors;
	FComponentTickList mTickableComponents;
	FComponentTickList mUUIDTickableComponents;
	TArray<UPrimitiveComponent*> mPrimitiveComponents;
	TArray<UActorComponent*> mNonPrimitiveRenderableComponents; // Primitive는 아닌데 렌더링 기능이 있는 컴포넌트.
    TArray<UActorComponent*> mUUIDRenderableComponents;
    TActiveTickList<AActor> ActiveActors;
    bool bLastUUIDTextVisible = true;
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
