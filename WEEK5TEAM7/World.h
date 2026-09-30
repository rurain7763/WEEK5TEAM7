#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"
#include "TMap.h"

class UPrimitiveComponent;
class UText3DComponent;

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

	// NOTE: 이번 프레임에 렌더링 대상이 된 컴포넌트를 등록. Unique 체크를 하지 않으므로, 렌더링 대상이 된 컴포넌트는 반드시 한 번만 등록해야함.
	void RequestRenderUpdate(UActorComponent* component);

	void RegisterActorComponents(AActor* actor);
	void UnregisterActorComponents(AActor* actor);

	TArray<AActor*>& GetActors() { return mActors; }
	const TArray<UPrimitiveComponent*>& GetPrimitiveComponents() const { return mPrimitiveComponents; }

	void Tick(float deltaTime);
	// 비교 실험용: Tick 시작 전에 기준 뷰의 위치를 한 번 전달합니다.
	void SetLODViewOrigin(const FVector& ViewOrigin) { mLODViewOrigin = ViewOrigin; }
	const FVector& GetLODViewOrigin() const { return mLODViewOrigin; }
	void Render(float deltaTime, FRenderCollector& outCollector);

private:
	int32 getActorIndex(uint32 actorUUID) const;
	// 변경된 리프와 조상만 설정 요약을 갱신합니다. 설정 편집 시에는 전체 요약을 갱신합니다.
	void RefreshBVHLODState(const FBVHNode* Node, uint64 SettingsRevision);

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
	TArray<UActorComponent*> mShouldRenderComponents; // 이번 프레임에 렌더링 대상이 된 컴포넌트. 렌더링 후 Clear()로 비워야 함.

	bool mbBVHDirty = true;
	FBVH<UPrimitiveComponent*> mBVH;
	FVector mLODViewOrigin;
};
