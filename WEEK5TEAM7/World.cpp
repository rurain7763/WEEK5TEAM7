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

UWorld::UWorld()
    : bLastUUIDTextVisible(FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
{
}

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
    RefreshTickRegistration(actor);
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
    ActiveActors.Remove(ActorToRemove);
    // 현재 컴포넌트 Tick에서 월드를 나가면 그 Actor의 남은 컴포넌트는 이번 프레임에 실행하지 않습니다.
    ActorToRemove->ActiveTickComponents.CancelTick();
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
    // 월드 밖에서 표시 옵션이 바뀐 뒤 재진입한 경우에도 현재 실행 조건을 반영합니다.
    if (bUUID && Component->GetOwner()) Component->GetOwner()->RefreshComponentTickRegistration(Component);
	if (PrimitiveComponent)
	{
		mPrimitiveComponents.Add(PrimitiveComponent);
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
	}
	else if (Registration.bRenderable)
	{
        auto& List = Registration.bUUID ? mUUIDRenderableComponents : mNonPrimitiveRenderableComponents;
		int32 index = List.Find(Component);
		if (index != -1)
		{
			List.RemoveAtSwap(index);
		}
	}
}

void UWorld::RefreshTickRegistration(AActor* Actor)
{
    if (Actor->GetWorld() != this) return;
    if (Actor->HasTickableComponents()) ActiveActors.Add(Actor);
    else ActiveActors.Remove(Actor);
}

void UWorld::MarkBoundsDirty(UActorComponent* Component)
{
	UPrimitiveComponent* PrimitiveComponent = Component->Cast<UPrimitiveComponent>();
	if (PrimitiveComponent)
	{
		mBVH.Refit(PrimitiveComponent, PrimitiveComponent->GetBoundingBox());
	}
}

void UWorld::RefreshUUIDTickVisibility()
{
    const bool bVisible = FShowFlags::Get().IsEnabled(EShowFlag::UUIDText);
    if (bLastUUIDTextVisible == bVisible) return;
    PROFILE_SCOPE("World/RefreshUUIDTickVisibility");
    bLastUUIDTextVisible = bVisible;
    // 매 프레임 전체 컴포넌트를 검사하지 않고, 표시 전환 시 UUID 목록만 한 번 갱신합니다.
    for (UActorComponent* Component : mUUIDRenderableComponents)
    {
        if (AActor* Owner = Component->GetOwner()) Owner->RefreshComponentTickRegistration(Component);
    }
}

void UWorld::Tick(float deltaTime)
{
    RefreshUUIDTickVisibility();
    {
        PROFILE_SCOPE("World/ActiveTick");
        ActiveActors.Tick(deltaTime);
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
		mbBVHDirty = false;
	}
}

void UWorld::Render(float deltaTime, FRenderCollector& outCollector)
{
    {
        PROFILE_SCOPE("World/CollectNonPrimitive");
        for (UActorComponent* Component : mNonPrimitiveRenderableComponents) Component->Render(outCollector);
    }
    // 목록 순회 전에 옵션을 검사하여 숨겨진 UUID 개수에 비례하는 비용을 없앱니다.
    if (FShowFlags::Get().IsEnabled(EShowFlag::UUIDText))
    {
        PROFILE_SCOPE("World/CollectUUID");
        for (UActorComponent* Component : mUUIDRenderableComponents) Component->Render(outCollector);
    }
    {
        PROFILE_SCOPE("World/BVHQuery");
        QueryStack.Empty();
        VisibleRanges.Empty();
        if (mBVH.IsValid()) QueryStack.Add(mBVH.GetRootNode());
        while (!QueryStack.IsEmpty())
        {
            FBVHNode* Node = QueryStack.Last();
            QueryStack.Pop();
            if (!Node) continue;
            const int32 Result = outCollector.Frustum.Intersects(Node->BoundingBox);
            if (Result == -1) continue;
            if (Result == 1 || Node->IsLeaf()) VisibleRanges.Add(Node->ItemRange);
            else
            {
                QueryStack.Add(Node->Left);
                QueryStack.Add(Node->Right);
            }
        }
    }
    {
        PROFILE_SCOPE("World/CollectPrimitives");
        // 기존 BVH의 연속 범위를 사용하여 개별 가시 객체 배열을 복사하지 않습니다.
		for (const auto& Range : VisibleRanges)
		{
			for (int32 I = 0; I < Range.Count; ++I)
			{
				mBVH.GetPayload(Range.Offset + I)->Render(outCollector);
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
