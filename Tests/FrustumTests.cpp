#include "../WEEK5TEAM7/FFrustum.h"
#include <cassert>
#include <limits>
#include <cstdio>

// 평면 추출과 독립적으로 AABB 모서리를 클립 공간에 변환하여 판정합니다.
EFrustumResult ReferenceClassify(const FMatrix& Matrix, const FAABB& Bounds)
{
    FVector Corners[8];
    Bounds.GetCorners(Corners);
    uint32 OutsideCounts[6]{};
    for (const FVector& Corner : Corners)
    {
        float Clip[4]{};
        for (uint32 Column = 0; Column < 4; ++Column)
            Clip[Column] = Corner.x * Matrix.M[0][Column] + Corner.y * Matrix.M[1][Column]
                + Corner.z * Matrix.M[2][Column] + Matrix.M[3][Column];
        const float Distances[6] = {Clip[3] + Clip[0], Clip[3] - Clip[0], Clip[3] + Clip[1],
            Clip[3] - Clip[1], Clip[2], Clip[3] - Clip[2]};
        for (uint32 Plane = 0; Plane < 6; ++Plane) if (Distances[Plane] < 0) ++OutsideCounts[Plane];
    }
    bool Inside = true;
    for (uint32 Count : OutsideCounts)
    {
        if (Count == 8) return EFrustumResult::Outside;
        if (Count != 0) Inside = false;
    }
    return Inside ? EFrustumResult::Inside : EFrustumResult::Intersecting;
}

int main()
{
    FFrustum Frustum;
    assert(Frustum.Classify({{100, 100, 100}, {101, 101, 101}}) == EFrustumResult::Intersecting);
    Frustum.Update(FMatrix::makeIdentity());
    assert(Frustum.IsValid());
    assert(Frustum.Classify({{-.5f, -.5f, .2f}, {.5f, .5f, .8f}}) == EFrustumResult::Inside);
    assert(Frustum.Classify({{-2, -2, -2}, {2, 2, 2}}) == EFrustumResult::Intersecting);
    assert(Frustum.Classify({{1, 0, .3f}, {2, .5f, .7f}}) == EFrustumResult::Intersecting);
    for (uint32 Axis = 0; Axis < 3; ++Axis)
    {
        FAABB Negative{{-.2f, -.2f, .2f}, {.2f, .2f, .8f}};
        FAABB Positive = Negative;
        Negative.Min[Axis] = -3; Negative.Max[Axis] = -2;
        Positive.Min[Axis] = 2; Positive.Max[Axis] = 3;
        assert(Frustum.Classify(Negative) == EFrustumResult::Outside);
        assert(Frustum.Classify(Positive) == EFrustumResult::Outside);
    }
    assert(Frustum.Classify({{0, 0, -.2f}, {.2f, .2f, -.1f}}) == EFrustumResult::Outside);
    assert(Frustum.Classify({{1, 1, 1}, {-1, -1, -1}}) == EFrustumResult::Intersecting);
    Frustum.Update(FMatrix{});
    assert(!Frustum.IsValid());

    uint32 Random = 31;
    auto Next = [&]
    {
        Random = Random * 1664525u + 1013904223u;
        return static_cast<float>(Random >> 8) / 16777216.f;
    };
    // 엔진의 통합 투영식과 카메라 이동·회전에 해당하는 행렬을 함께 검사합니다.
    for (float Ratio : {0.f, .25f, .5f, .75f, 1.f})
    {
        const float Near = .1f, Far = 100.f, Distance = 5.f;
        FMatrix Projection;
        Projection.M[0][0] = 1.2f / Distance;
        Projection.M[1][1] = 1.6f / Distance;
        Projection.M[2][2] = ((1 - Ratio) + Ratio * Far / Distance) / (Far - Near);
        Projection.M[2][3] = Ratio / Distance;
        Projection.M[3][2] = -Near * Projection.M[2][2];
        Projection.M[3][3] = 1 - Ratio;
        FMatrix View = FMatrix::makeIdentity();
        View.M[0][0] = .8f; View.M[0][2] = -.6f;
        View.M[2][0] = .6f; View.M[2][2] = .8f;
        View.M[3][0] = 3; View.M[3][1] = -2; View.M[3][2] = 7;
        const FMatrix Matrix = View * Projection;
        Frustum.Update(Matrix);
        assert(Frustum.IsValid());
        for (uint32 I = 0; I < 2000; ++I)
        {
            FVector Min{Next() * 200 - 100, Next() * 200 - 100, Next() * 200 - 100};
            FAABB Bounds{Min, Min + FVector{.1f + Next() * 5, .1f + Next() * 5, .1f + Next() * 5}};
            const auto Reference = ReferenceClassify(Matrix, Bounds);
            const auto Actual = Frustum.Classify(Bounds);
            // 허용 오차는 경계의 객체를 통과시키는 쪽으로만 작용해야 합니다.
            if (Actual == EFrustumResult::Outside) assert(Reference == EFrustumResult::Outside);
            if (Actual == EFrustumResult::Inside) assert(Reference == EFrustumResult::Inside);
        }
    }
    std::puts("Frustum tests passed: six planes, boundaries, invalid inputs, 10000 projection comparisons.");
}
