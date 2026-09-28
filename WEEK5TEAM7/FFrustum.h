#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "FAABB.h"
#include <cmath>

struct FPlane
{
	FVector Normal = FVector(0.f, 0.f, 0.f);
	float D = 0.0f;
	FVector AbsNormal = FVector(0.f, 0.f, 0.f);

	FPlane() = default;
	FPlane(float a, float b, float c, float d)
	{
		const float length = std::sqrt(a * a + b * b + c * c);
		if (length > 1e-6f)
		{
			const float invLength = 1.0f / length;
			Normal = FVector(a * invLength, b * invLength, c * invLength);
			D = d * invLength;
			AbsNormal = FVector(std::abs(Normal.x), std::abs(Normal.y), std::abs(Normal.z));
		}
		else
		{
			Normal = FVector(0.f, 0.f, 0.f);
			D = 0.0f;
			AbsNormal = FVector(0.f, 0.f, 0.f);
		}
	}

	FPlane(const FVector& InNormal, float InD)
		: Normal(InNormal), D(InD), AbsNormal(std::abs(InNormal.x), std::abs(InNormal.y), std::abs(InNormal.z))
	{
	}

	// 점과 평면 사이의 부호 있는 거리 (양수면 법선 방향/절두체 내부, 음수면 외부)
	inline float Dot(const FVector& Point) const
	{
		return Normal.x * Point.x + Normal.y * Point.y + Normal.z * Point.z + D;
	}
};

struct FFrustum
{
	enum EPlaneIndex
	{
		Near = 0,
		Far,
		Left,
		Right,
		Top,
		Bottom,
		PlaneCount = 6
	};

	FPlane Planes[PlaneCount];

	FFrustum() = default;

	// ViewProjection 행렬로부터 6개 절두체 평면 추출 (Gribb-Hartmann 방식)
	// 행렬 규약: 행벡터 v' = v * M
	static FFrustum FromMatrix(const FMatrix& M)
	{
		FFrustum Frustum;

		// Near 평면: z' >= 0
		Frustum.Planes[Near] = FPlane(
			M.M[0][2],
			M.M[1][2],
			M.M[2][2],
			M.M[3][2]
		);

		// Far 평면: z' <= w'  <=>  w' - z' >= 0
		Frustum.Planes[Far] = FPlane(
			M.M[0][3] - M.M[0][2],
			M.M[1][3] - M.M[1][2],
			M.M[2][3] - M.M[2][2],
			M.M[3][3] - M.M[3][2]
		);

		// Left 평면: x' >= -w'  <=>  w' + x' >= 0
		Frustum.Planes[Left] = FPlane(
			M.M[0][3] + M.M[0][0],
			M.M[1][3] + M.M[1][0],
			M.M[2][3] + M.M[2][0],
			M.M[3][3] + M.M[3][0]
		);

		// Right 평면: x' <= w'  <=>  w' - x' >= 0
		Frustum.Planes[Right] = FPlane(
			M.M[0][3] - M.M[0][0],
			M.M[1][3] - M.M[1][0],
			M.M[2][3] - M.M[2][0],
			M.M[3][3] - M.M[3][0]
		);

		// Top 평면: y' <= w'  <=>  w' - y' >= 0
		Frustum.Planes[Top] = FPlane(
			M.M[0][3] - M.M[0][1],
			M.M[1][3] - M.M[1][1],
			M.M[2][3] - M.M[2][1],
			M.M[3][3] - M.M[3][1]
		);

		// Bottom 평면: y' >= -w'  <=>  w' + y' >= 0
		Frustum.Planes[Bottom] = FPlane(
			M.M[0][3] + M.M[0][1],
			M.M[1][3] + M.M[1][1],
			M.M[2][3] + M.M[2][1],
			M.M[3][3] + M.M[3][1]
		);

		return Frustum;
	}

	// AABB 박스가 절두체 내부에 있거나 걸쳐있는지 검사
	inline bool Intersects(const FAABB& Box) const
	{
		const FVector Center = (Box.Min + Box.Max) * 0.5f;
		const FVector Extent = (Box.Max - Box.Min) * 0.5f;

		for (int i = 0; i < PlaneCount; ++i)
		{
			const FPlane& Plane = Planes[i];

			// 박스의 반경을 평면 법선에 투영
			const float Radius = Extent.x * Plane.AbsNormal.x +
			                     Extent.y * Plane.AbsNormal.y +
			                     Extent.z * Plane.AbsNormal.z;

			const float Distance = Plane.Dot(Center);

			if (Distance < -Radius)
			{
				return false;
			}
		}

		return true;
	}
};
