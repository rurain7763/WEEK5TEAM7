#pragma once

#include "Object.h"
#include "Actor.h"
#include "RenderInfo.h"
#include "FFrustum.h"

class UPrimitiveComponent;
class UText3DComponent;

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

	void RegisterComponent(UActorComponent* component);
	void UnregisterComponent(UActorComponent* component);
	void RegisterActorComponents(AActor* actor);
	void UnregisterActorComponents(AActor* actor);

	TArray<AActor*>& GetActors() { return mActors; }
	const TArray<UPrimitiveComponent*>& GetPrimitiveComponents() const { return mPrimitiveComponents; }
	const TArray<UText3DComponent*>& GetText3DComponents() const { return mText3DComponents; }

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
	TArray<UText3DComponent*> mText3DComponents;
};
