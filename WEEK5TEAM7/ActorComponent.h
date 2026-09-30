#pragma once

#include "Object.h"

struct FRenderInfo;
class FRenderCollector;

enum EActorComponentFlags
{
	EditorOnly = 1 << 0, // 에디터에서만 존재하는 컴포넌트. 게임에서는 제거된다.
	DoNotSerialize = 1 << 1, // 직렬화하지 않는다. (에디터에서만 존재하는 컴포넌트는 기본적으로 직렬화하지 않는다.)
	Renderable = 1 << 2, // 렌더링 가능한 컴포넌트. (UPrimitiveComponent 등)
	Tickable = 1 << 3, // 매 프레임 갱신이 필요한 컴포넌트만 활성 목록에 등록합니다.
};

class UActorComponent : public UObject
{
	REFLECT_CLASS(UActorComponent, UObject)
public:
	UActorComponent();
	virtual ~UActorComponent();

	void SetOwner(AActor* owner);
	AActor* GetOwner() const;
	void SetTickable(bool bTickable);
	bool IsTickable() const { return (mComponentFlags & EActorComponentFlags::Tickable) != 0; }

	// Todo: Make as pure class
	virtual void Tick(float deltaTime);
	virtual void Render(FRenderCollector& RenderCollector);
	virtual void GetRenderInfos(TArray<FRenderInfo>* outRenderInfos) const;

	inline void SetEditorOnly(bool bEditorOnly)
	{
		if (bEditorOnly)
		{
			mComponentFlags |= EActorComponentFlags::EditorOnly;
		}
		else
		{
			mComponentFlags &= ~EActorComponentFlags::EditorOnly;
		}
	}

	inline bool IsEditorOnly() const { return (mComponentFlags & EActorComponentFlags::EditorOnly) != 0; }

	inline void SetDoNotSerialize(bool bDoNotSerialize)
	{
		if (bDoNotSerialize)
		{
			mComponentFlags |= EActorComponentFlags::DoNotSerialize;
		}
		else
		{
			mComponentFlags &= ~EActorComponentFlags::DoNotSerialize;
		}
	}

	inline bool ShouldSerialize() const { return (mComponentFlags & EActorComponentFlags::DoNotSerialize) == 0; }

	inline void SetRenderable(bool bRenderable)
	{
		if (bRenderable)
		{
			mComponentFlags |= EActorComponentFlags::Renderable;
		}
		else
		{
			mComponentFlags &= ~EActorComponentFlags::Renderable;
		}
	}

	inline bool IsRenderable() const { return (mComponentFlags & EActorComponentFlags::Renderable) != 0; }

protected:
	AActor* mOwner;

private:
	uint32 mComponentFlags = 0;
};
