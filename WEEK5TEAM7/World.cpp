#include "World.h"

#include <format>

#include "RenderInfo.h"
#include "JsonUtil.h"
#include "Console.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "FBVH.h"
#include "UTextComponent.h"
#include "UTextComponent.h"
#include "ShowFlags.h"

UWorld::~UWorld()
{
	mPrimitiveComponents.Empty();

	for (AActor* removeActor : mActors)
	{
		removeActor->mWorld = nullptr;
		FObjectFactory::DestroyObject(removeActor);
	}
}

void UWorld::SerializeClass(json::JSON& outJson) const
{
	UObject::SerializeClass(outJson);
	json::JSON actorsJson = json::JSON::Make(json::JSON::Class::Array);

	for (const AActor* actor : mActors)
	{
		json::JSON actorJson;
		actor->SerializeClass(actorJson);
		actorsJson.append(std::move(actorJson));
	}

	outJson["Properties"]["mActors"] = actorsJson;
}

void UWorld::DeserializeClass(const json::JSON& inJson)
{
	UObject::DeserializeClass(inJson);

	const json::JSON& propertiesJson = inJson.at("Properties");

	if (!propertiesJson.hasKey("mActors") || propertiesJson.at("mActors").JSONType() != json::JSON::Class::Array)
	{
		throw std::runtime_error(std::format("{}: mActors requires an array", GetClass()->Name));
	}

	const json::JSON& actorsJson = propertiesJson.at("mActors");

	for (const auto& actorJson : actorsJson.ArrayRange())
	{
		if (!actorJson.hasKey("ClassName") || actorJson.at("ClassName").JSONType() != json::JSON::Class::String)
		{
			throw std::runtime_error(std::format("{}: ClassName requires a string", GetClass()->Name));
		}
		FString className(actorJson.at("ClassName").ToString());

		const FClassInfo* classInfo = FObjectFactory::GetClassInfoByName(className);
		if (!classInfo)
		{
			throw std::runtime_error(std::format("{}: Unknown class name: {}", GetClass()->Name, className));
		}
		AActor* actor = static_cast<AActor*>(FObjectFactory::LoadObject(classInfo, actorJson));
		AddActor(actor);
	}
}

void UWorld::RegisterActorComponents(AActor* actor)
{
	if (!actor) return;

	for (UActorComponent* component : actor->GetComponents())
	{
		RegisterComponent(component);
	}
}

void UWorld::UnregisterActorComponents(AActor* actor)
{
	if (!actor) return;

	for (UActorComponent* component : actor->GetComponents())
	{
		UnregisterComponent(component);
	}
}

void UWorld::AddActor(AActor* actor)
{
	assert(actor != nullptr);
	assert(getActorIndex(actor->UUID) == -1);

	actor->mWorld = this;
	for (UActorComponent* component : actor->GetComponents())
	{
		RegisterComponent(component);
	}

	mActors.Add(actor);

	// TODO: 전처리를 통해 에디터 모드가 아니면 아래 코드를 컴파일하지 않게 막아야함.
	actor->CreateEditorComponents();
}

bool UWorld::RemoveActor(uint32 uuid)
{
	int32 ActorIndex = getActorIndex(uuid);
	if (ActorIndex == -1)
	{
		return false;
	}

	AActor* ActorToRemove = mActors[ActorIndex];
	for (UActorComponent* component : ActorToRemove->GetComponents())
	{
		UnregisterComponent(component);
	}
	ActorToRemove->mWorld = nullptr;

	mActors.RemoveAtSwap(ActorIndex);

	return true;
}

void UWorld::RegisterComponent(UActorComponent* Component)
{
	RefreshComponentTick(Component);
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mPrimitiveComponents.Add(PrimitiveComponent);
		mbBVHDirty = true;
	}
	else if (Component->IsRenderable())
	{
		mNonPrimitiveRenderableComponents.Add(Component);
	}
}

void UWorld::UnregisterComponent(UActorComponent* Component)
{
	mTickableComponents.Remove(Component);
	mUUIDTickableComponents.Remove(Component);
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		int32 index = mPrimitiveComponents.Find(PrimitiveComponent);
		if (index != -1)
		{
			mPrimitiveComponents.RemoveAtSwap(index);
			mbBVHDirty = true;
		}
	}
	else if (Component->IsRenderable())
	{
		int32 index = mNonPrimitiveRenderableComponents.Find(Component);
		if (index != -1)
		{
			mNonPrimitiveRenderableComponents.RemoveAtSwap(index);
		}
	}
}

void UWorld::FComponentTickList::Add(UActorComponent* Component)
{
	if (!Indices.Contains(Component))
		Indices.Add(Component, Components.Add(Component));
}

void UWorld::FComponentTickList::Remove(UActorComponent* Component)
{
	const uint32* Found = Indices.Find(Component);
	if (!Found) return;
	const uint32 Index = *Found;
	Indices.Remove(Component);
	if (bTicking)
	{
		// Tick 내부에서 제거되어도 뒤의 컴포넌트를 건너뛰거나 해제된 포인터를 호출하지 않습니다.
		Components[Index] = nullptr;
		bNeedsCompaction = true;
		return;
	}
	Components.RemoveAtSwap(Index);
	if (Index < static_cast<uint32>(Components.Num()))
		*Indices.Find(Components[Index]) = Index;
}

void UWorld::FComponentTickList::Tick(float DeltaTime, int32 Count)
{
	bTicking = true;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (UActorComponent* Component = Components[Index])
			Component->Tick(DeltaTime);
	}
	bTicking = false;
	if (bNeedsCompaction)
	{
		// 순회 중 삭제가 발생한 프레임에만 빈 슬롯을 정리합니다.
		for (int32 Index = Components.Num() - 1; Index >= 0; --Index)
		{
			if (Components[Index]) continue;
			Components.RemoveAtSwap(Index);
			if (Index < Components.Num())
				*Indices.Find(Components[Index]) = static_cast<uint32>(Index);
		}
		bNeedsCompaction = false;
	}
}

void UWorld::RefreshComponentTick(UActorComponent* Component)
{
	if (!Component->IsTickable())
	{
		mTickableComponents.Remove(Component);
		mUUIDTickableComponents.Remove(Component);
		return;
	}
	if (Component->Cast<UText3DComponent>())
		mUUIDTickableComponents.Add(Component);
	else
		mTickableComponents.Add(Component);
}

void UWorld::MarkBoundsDirty(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mBVH.Refit(PrimitiveComponent, PrimitiveComponent->GetBoundingBox());
	}
}

void UWorld::Tick(float deltaTime)
{
	// 전체 Actor/Component 목록을 훑지 않고 실제 갱신 대상만 순회합니다.
	mTickableComponents.Tick(deltaTime, mTickableComponents.Components.Num());
	// 숨긴 UUID는 컴포넌트별 조건 검사도 하지 않고 목록 전체를 건너뜁니다.
	if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
		mUUIDTickableComponents.Tick(deltaTime, mUUIDTickableComponents.Components.Num());

	if (mbBVHDirty)
	{
		mBVH.Release();
		for (UPrimitiveComponent* primitiveComponent : mPrimitiveComponents)
		{
			mBVH.AddItem(primitiveComponent, primitiveComponent->GetBoundingBox());
		}
		mBVH.Build();
		mbBVHDirty = false;
	}
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
	for (UActorComponent* Component : mNonPrimitiveRenderableComponents)
	{
		Component->Render(outCollector);
	}

	if (FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
	{
		if (mBVH.IsValid())
		{
			TArray<FBVHNode*> NodeStack;
			NodeStack.Add(mBVH.GetRootNode());
			while (NodeStack.Num() > 0)
			{
				FBVHNode* CurrentNode = NodeStack.Last();
				NodeStack.Pop();

				if (CurrentNode == nullptr)
				{
					continue;
				}

				int32 CollisionResult = outCollector.Frustum.Intersects(CurrentNode->BoundingBox);
				if (CollisionResult == -1)
				{
					continue;
				}

				if (CollisionResult == 1 || CurrentNode->IsLeaf())
				{
					for (int32 i = 0; i < CurrentNode->ItemRange.Count; ++i)
					{
						UPrimitiveComponent* Object = mBVH.GetPayload(CurrentNode->ItemRange.Offset + i);
						Object->Render(outCollector);
					}
				}
				else
				{
					NodeStack.Add(CurrentNode->Left);
					NodeStack.Add(CurrentNode->Right);
				}
			}
		}
	}
	
	outCollector.BVH = &mBVH;
}

int32 UWorld::getActorIndex(uint32 actorUUID) const
{
	for (uint32 i = 0; i < mActors.Num(); ++i)
	{
		if (mActors[i]->UUID == actorUUID)
		{
			return i;
		}
	}

	return -1;
}
