#pragma once

#include "Core.h"
#include "FAABB.h"
#include <cmath>

enum class EFrustumResult { Outside, Intersecting, Inside };

class FFrustum
{
public:
    void Update(const FMatrix& ViewProjection)
    {
        bValid = true;
        // D3D 클립 범위는 -w <= x,y <= w, 0 <= z <= w입니다.
        for (uint32 Row = 0; Row < 4; ++Row)
        {
            Planes[0][Row] = ViewProjection.M[Row][3] + ViewProjection.M[Row][0];
            Planes[1][Row] = ViewProjection.M[Row][3] - ViewProjection.M[Row][0];
            Planes[2][Row] = ViewProjection.M[Row][3] + ViewProjection.M[Row][1];
            Planes[3][Row] = ViewProjection.M[Row][3] - ViewProjection.M[Row][1];
            Planes[4][Row] = ViewProjection.M[Row][2];
            Planes[5][Row] = ViewProjection.M[Row][3] - ViewProjection.M[Row][2];
        }
        for (uint32 Index = 0; Index < 6; ++Index)
        {
            auto& Plane = Planes[Index];
            const float Length = std::sqrt(Plane[0] * Plane[0] + Plane[1] * Plane[1] + Plane[2] * Plane[2]);
            if (!std::isfinite(Length) || Length <= 0.f || !std::isfinite(Plane[3]))
            {
                bValid = false;
                return;
            }
            for (float& Value : Plane)
            {
                Value /= Length;
                if (!std::isfinite(Value)) bValid = false;
            }
            // 모든 AABB가 재사용할 값이므로 법선의 절댓값은 평면 갱신 시 한 번만 계산합니다.
            AbsNormals[Index] = FVector{std::fabs(Plane[0]), std::fabs(Plane[1]), std::fabs(Plane[2])};
        }
    }

    EFrustumResult Classify(const FAABB& Bounds) const
    {
        // 잘못된 입력은 보수적으로 통과시켜 메시가 사라지는 것을 방지합니다.
        if (!bValid) return EFrustumResult::Intersecting;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
            if (!std::isfinite(Bounds.Min[Axis]) || !std::isfinite(Bounds.Max[Axis]) || Bounds.Min[Axis] > Bounds.Max[Axis])
                return EFrustumResult::Intersecting;

        // 중심과 반길이는 평면마다 다시 계산하지 않고 AABB당 한 번만 구합니다.
        const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
        const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;

        bool bInside = true;
        for (uint32 Index = 0; Index < 6; ++Index)
        {
            const auto& Plane = Planes[Index];
            const FVector& AbsNormal = AbsNormals[Index];
            // 법선은 Frustum 안쪽을 향합니다. Distance는 중심의 부호 있는 거리입니다.
            const float Distance = Plane[0] * Center.x + Plane[1] * Center.y + Plane[2] * Center.z + Plane[3];
            // 반길이를 법선 방향으로 투영한 반경입니다. AABB의 거리 범위는 Distance ± Radius입니다.
            const float Radius = AbsNormal.x * Extent.x + AbsNormal.y * Extent.y + AbsNormal.z * Extent.z;

            // 가장 안쪽인 점까지 평면 밖이면 AABB 전체를 제외할 수 있습니다.
            if (Distance + Radius < -Tolerance) return EFrustumResult::Outside;
            // 가장 바깥쪽인 점이 평면에 걸치면 완전 내부로 판정하지 않습니다.
            // 경계 오차 범위도 통과시켜 경계의 메시가 깜빡이며 사라지는 것을 방지합니다.
            if (Distance - Radius <= Tolerance) bInside = false;
        }
        return bInside ? EFrustumResult::Inside : EFrustumResult::Intersecting;
    }

    bool IsValid() const { return bValid; }

private:
    static constexpr float Tolerance = 1.e-4f;
    float Planes[6][4]{};
    FVector AbsNormals[6]{};
    bool bValid = false;
};
