#include "World.h"

#include <format>

#include "RenderInfo.h"
#include "JsonUtil.h"
#include "Console.h"
#include "ObjectFactory.h"
#include "PrimitiveComponent.h"
#include "FBVH.h"
#include "FInstrumentor.h"
#include "UTextComponent.h"
#include "ShowFlags.h"
#include "FHiZOcclusionManager.h"

UWorld::~UWorld()
{
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
    if (ComponentRegistrations.Contains(Component)) return;
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
    const bool bUUID = Component->IsA<UText3DComponent>();
    ComponentRegistrations.Add(Component, { PrimitiveComponent, Component->IsRenderable(), bUUID });
    RefreshComponentTick(Component);
	if (PrimitiveComponent)
	{
		mPrimitiveComponents.Add(PrimitiveComponent);
		mShouldRenderComponents.Add(Component);
		mbBVHDirty = true;
	}
	else if (Component->IsRenderable())
	{
        if (bUUID) mUUIDRenderableComponents.Add(Component);
        else mNonPrimitiveRenderableComponents.Add(Component);
	}
}

void UWorld::UnregisterComponent(UActorComponent* Component)
{
    const auto* Found = ComponentRegistrations.Find(Component);
    if (!Found) return;
    const FComponentRegistration Registration = *Found;
    // 소멸 중 가상 타입에 의존하지 않고 등록 당시의 목록에서 제거합니다.
    auto& TickList = Registration.bUUID ? mUUIDTickableComponents : mTickableComponents;
    TickList.Remove(Component);
    ComponentRegistrations.Remove(Component);
	UPrimitiveComponent* PrimitiveComponent = Registration.Primitive;
	if (PrimitiveComponent)
	{
		int32 index = mPrimitiveComponents.Find(PrimitiveComponent);
		if (index != -1)
		{
			mPrimitiveComponents.RemoveAtSwap(index);
			mbBVHDirty = true;
		}

		index = mShouldRenderComponents.Find(Component);
		if (index != -1)
		{
			mShouldRenderComponents.RemoveAtSwap(index);
		}
	}
	else if (Registration.bRenderable)
	{
        auto& List = Registration.bUUID ? mUUIDRenderableComponents : mNonPrimitiveRenderableComponents;
		int32 index = List.Find(Component);
		if (index != -1)
		{
			List.RemoveAtSwap(index);
		}

		index = mShouldRenderComponents.Find(Component);
		if (index != -1)
		{
			mShouldRenderComponents.RemoveAtSwap(index);
		}
	}
}

void UWorld::RefreshComponentTick(UActorComponent* Component)
{
    // Owner만 연결되고 아직 월드에 등록되지 않은 컴포넌트는 실행하지 않습니다.
    const FComponentRegistration* Registration = ComponentRegistrations.Find(Component);
    if (!Registration) return;
    auto& TickList = Registration->bUUID ? mUUIDTickableComponents : mTickableComponents;
    if (Component->IsTickable()) TickList.Add(Component);
    else TickList.Remove(Component);
}

void UWorld::MarkBoundsDirty(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mBVH.Refit(PrimitiveComponent, PrimitiveComponent->GetBoundingBox());
		mBVH.GetAllBoundingBoxes(mCachedEntryAABBs);
		mbAABBsDirty = true;
	}
}

void UWorld::RequestRenderUpdate(UActorComponent* Component)
{
	mShouldRenderComponents.Add(Component);
}

void UWorld::Tick(float deltaTime)
{
    {
        PROFILE_SCOPE("World/ActiveTick");
        mTickableComponents.Tick(deltaTime);
        // 숨겨진 UUID는 컴포넌트 수와 관계없이 목록 전체를 건너뜁니다.
        if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
        {
            PROFILE_SCOPE("World/UUIDTick");
            mUUIDTickableComponents.Tick(deltaTime);
        }
    }

	if (mbBVHDirty)
	{
        PROFILE_SCOPE("World/BVHBuild");
		mBVH.Release();
		for (UPrimitiveComponent* primitiveComponent : mPrimitiveComponents)
		{
			mBVH.AddItem(primitiveComponent, primitiveComponent->GetBoundingBox());
		}
		mBVH.Build();
		mBVH.GetAllBoundingBoxes(mCachedEntryAABBs);
		mbBVHDirty = false;
		mbAABBsDirty = true;
	}
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
	{
		PROFILE_SCOPE("World/CollectNonPrimitive");
		for (UActorComponent* Component : mNonPrimitiveRenderableComponents)
		{
			Component->Render(outCollector);
		}
	}

	// 목록 순회 전에 옵션을 검사하여 숨겨진 UUID 개수에 비례하는 비용을 없앱니다.
	if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
	{
		PROFILE_SCOPE("World/CollectUUID");
		for (UActorComponent* Component : mUUIDRenderableComponents)
		{
			Component->Render(outCollector);
		}
	}

	for (UActorComponent* Component : mShouldRenderComponents)
	{
		Component->Render(outCollector);
	}
	mShouldRenderComponents.Empty();

	if (FShowFlags::Get().IsEnabled(EShowFlag::Primitive))
	{
		if (mBVH.IsValid())
		{
			PROFILE_SCOPE("World/BVHQuery");
			const bool bOcclusionEnabled = FShowFlags::Get().IsEnabled(EShowFlag::OcclusionCulling);

			QueryStack.Empty();
			QueryStack.Add(mBVH.GetRootNode());

			while (!QueryStack.IsEmpty())
			{
				FBVHNode* CurrentNode = QueryStack.Last();
				QueryStack.Pop();
				if (!CurrentNode) continue;

				int32 CollisionResult = outCollector.Frustum.Intersects(CurrentNode->BoundingBox);
				if (CollisionResult == -1)
				{
					continue;
				}

				if (CollisionResult == 1 || CurrentNode->IsLeaf())
				{
					for (int32 i = 0; i < CurrentNode->ItemRange.Count; ++i)
					{
						int32 EntryIndex = CurrentNode->ItemRange.Offset + i;
						if (bOcclusionEnabled && FHiZOcclusionManager::Get().IsOccluded(EntryIndex))
						{
							FHiZOcclusionManager::Get().IncrementCulledCount();
							continue;
						}

						UPrimitiveComponent* Object = mBVH.GetPayload(EntryIndex);
						if (Object && Object->GetRenderProxy())
						{
							Object->GetRenderProxy()->Submit();
						}
					}
				}
				else
				{
					QueryStack.Add(CurrentNode->Left);
					QueryStack.Add(CurrentNode->Right);
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
