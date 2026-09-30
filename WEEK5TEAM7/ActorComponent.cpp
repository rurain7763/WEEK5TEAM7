#include "ActorComponent.h"
#include "RenderInfo.h"
#include "World.h"

UActorComponent::UActorComponent()
	: mOwner(nullptr)
{
}

UActorComponent::~UActorComponent()
{
	SetTickable(false);
}

void UActorComponent::SetOwner(AActor* owner)
{
	assert(mOwner == nullptr || owner == nullptr);

	mOwner = owner;
}

AActor* UActorComponent::GetOwner() const
{
	return mOwner;
}

void UActorComponent::SetTickable(bool bTickable)
{
	if (IsTickable() == bTickable) return;
	if (bTickable)
		mComponentFlags |= EActorComponentFlags::Tickable;
	else
		mComponentFlags &= ~EActorComponentFlags::Tickable;

	// 생성자에서는 플래그만 설정하고, 월드에 속한 이후 변경부터 목록을 갱신합니다.
	if (mOwner && mOwner->GetWorld())
		mOwner->GetWorld()->RefreshComponentTick(this);
}

void UActorComponent::Tick(float deltaTime)
{
}

void UActorComponent::Render(FRenderCollector& RenderCollector)
{
	// Todo: Do nothing, must override, some components may not call Update()
	// assert(false);
}

void UActorComponent::GetRenderInfos(TArray<FRenderInfo>* outRenderInfos) const
{
	// Todo: Do nothing, must override, some components may not call GetRenderInfos()
	// assert(false);
}
