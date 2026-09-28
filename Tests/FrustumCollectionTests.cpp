#include "../WEEK5TEAM7/World.h"
#include "../WEEK5TEAM7/PrimitiveComponent.h"
#include <cassert>
#include <cstdio>

LRESULT CALLBACK WndProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return DefWindowProc(Window, Message, WParam, LParam);
}

class FTestComponent : public UPrimitiveComponent
{
public:
    FAABB Bounds;
    bool bHasBounds = true;
    uint32 RenderCalls = 0;
    bool GetCullingBounds(FAABB& Out) const override
    {
        Out = Bounds;
        return bHasBounds;
    }
    void Render(FRenderCollector& Collector) override
    {
        ++RenderCalls;
        Collector.RenderInfos.Add(FRenderInfo{});
    }
};

int main()
{
    UWorld World;
    AActor Actor;
    FTestComponent Visible, Hidden, Unbounded;
    Visible.UUID = 1; Hidden.UUID = 2; Unbounded.UUID = 3;
    Visible.Bounds = {{-.2f, -.2f, .2f}, {.2f, .2f, .8f}};
    Hidden.Bounds = {{5, 5, 5}, {6, 6, 6}};
    Unbounded.Bounds = Hidden.Bounds;
    Unbounded.bHasBounds = false;
    Actor.AddComponent(&Visible);
    Actor.AddComponent(&Hidden);
    Actor.AddComponent(&Unbounded);
    World.GetActors().Add(&Actor);

    FRenderCollector Collector;
    Collector.Frustum.Update(FMatrix::makeIdentity());
    Collector.bEnableFrustumCulling = true;
    World.Render(0, Collector);
    assert(Collector.RenderInfos.Num() == 2);
    assert(Collector.PickTargets.Num() == 3);
    assert(Collector.CullingStats.Tested == 2 && Collector.CullingStats.Culled == 1);
    assert(Hidden.RenderCalls == 0 && Unbounded.RenderCalls == 1);

    // 같은 컴포넌트도 다음 뷰포트의 Frustum으로 다시 판정합니다.
    FMatrix Moved = FMatrix::makeIdentity();
    Moved.M[3][0] = -5.5f; Moved.M[3][1] = -5.5f; Moved.M[3][2] = -5.f;
    Collector.Frustum.Update(Moved);
    World.Render(0, Collector);
    assert(Collector.RenderInfos.Num() == 2 && Collector.PickTargets.Num() == 3);
    assert(Hidden.RenderCalls == 1 && Visible.RenderCalls == 1);

    Collector.bEnableFrustumCulling = false;
    World.Render(0, Collector);
    assert(Collector.RenderInfos.Num() == 3 && Collector.PickTargets.Num() == 3);
    assert(Collector.CullingStats.Tested == 0);

    // 스택 객체는 테스트 종료 전에 소유 컨테이너에서 분리합니다.
    World.GetActors().Empty();
    Actor.RemoveComponent(1); Actor.RemoveComponent(2); Actor.RemoveComponent(3);
    std::puts("Collection tests passed: visible-only collection, picking, unbounded components, viewport change, OFF.");
}
