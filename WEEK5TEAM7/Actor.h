#pragma once

#include "Object.h"
#include "ActorComponent.h"
#include "TActiveTickList.h"

class UWorld;
struct FRenderInfo;
struct FTransform;
class USceneComponent;
class FRenderCollector;

class AActor : public UObject
{
	REFLECT_CLASS(AActor, UObject)
public:
	AActor() = default;
	virtual ~AActor();

	void Initialize();

	virtual void SerializeClass(json::JSON& outJson) const override;
	virtual void DeserializeClass(const json::JSON& inJson) override;

	void AddComponent(UActorComponent* actorComponent);
	void AddRootSceneComponent(USceneComponent* sceneComponent);
	USceneComponent* GetRootComponent() const;
	bool RemoveComponent(uint32 componentUUID);
	inline const TArray<UActorComponent*>& GetComponents() const { return mComponents; }

	virtual void CreateEditorComponents();

	const FTransform& GetTransform() const;

	virtual void Tick(float deltaTime);
    // 별도의 Actor 활성 플래그 없이 실제 Tick 실행 조건을 만족하는 컴포넌트로 결정합니다.
    bool HasTickableComponents() const { return ActiveTickComponents.Num() != 0; }
    uint32 GetTickableComponentCount() const { return ActiveTickComponents.Num(); }
	virtual void Render(FRenderCollector& RenderCollector);

	void GetRenderInfos(TArray<FRenderInfo>* outRenderInfos) const;
	bool GetFirstRenderInfo(FRenderInfo& outRenderInfo) const;

	void SetLocation(FVector location);
	void SetRotation(FRotator rotation);
	void SetScale(FVector scale);

	inline UWorld* GetWorld() const { return mWorld; }

private:
	int32 getComponentIndex(int32 componentUUID) const;
    // 등록/활성 여부 변경 때만 호출하며, 프레임마다 전체 컴포넌트를 검색하지 않습니다.
    void RefreshComponentTickRegistration(UActorComponent* Component);

private:
	friend class UWorld;
    friend class UActorComponent;

	UWorld* mWorld = nullptr;
    TActiveTickList<UActorComponent> ActiveTickComponents;

	USceneComponent* mRootComponent = nullptr;
	TArray<UActorComponent*> mComponents;
	bool mbPressed = false;
	bool mbStarted = false;
};

inline const FVector Up = FVector(0, 0, 1);
inline const FVector Right = FVector(0, 1, 0);
inline const FVector Front = FVector(1, 0, 0);

