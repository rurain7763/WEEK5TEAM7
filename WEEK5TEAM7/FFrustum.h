#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "FAABB.h"
#include <cmath>

struct FPlane
{
	FVector Normal = FVector(0.f, 0.f, 0.f);
	float D = 0.0f;

	FPlane() = default;
	FPlane(float a, float b, float c, float d)
	{
		const float length = std::sqrt(a * a + b * b + c * c);
		if (length > 1e-6f)
		{
			const float invLength = 1.0f / length;
			Normal = FVector(a * invLength, b * invLength, c * invLength);
			D = d * invLength;
		}
		else
		{
			Normal = FVector(0.f, 0.f, 0.f);
			D = 0.0f;
		}
	}

	FPlane(const FVector& InNormal, float InD)
		: Normal(InNormal), D(InD)
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

		// 1. Near 평면: z' >= 0
		Frustum.Planes[Near] = FPlane(
			M.M[0][2],
			M.M[1][2],
			M.M[2][2],
			M.M[3][2]
		);

		// 2. Far 평면: z' <= w'  <=>  w' - z' >= 0
		Frustum.Planes[Far] = FPlane(
			M.M[0][3] - M.M[0][2],
			M.M[1][3] - M.M[1][2],
			M.M[2][3] - M.M[2][2],
			M.M[3][3] - M.M[3][2]
		);

		// 3. Left 평면: x' >= -w'  <=>  w' + x' >= 0
		Frustum.Planes[Left] = FPlane(
			M.M[0][3] + M.M[0][0],
			M.M[1][3] + M.M[1][0],
			M.M[2][3] + M.M[2][0],
			M.M[3][3] + M.M[3][0]
		);

		// 4. Right 평면: x' <= w'  <=>  w' - x' >= 0
		Frustum.Planes[Right] = FPlane(
			M.M[0][3] - M.M[0][0],
			M.M[1][3] - M.M[1][0],
			M.M[2][3] - M.M[2][0],
			M.M[3][3] - M.M[3][0]
		);

		// 5. Top 평면: y' <= w'  <=>  w' - y' >= 0
		Frustum.Planes[Top] = FPlane(
			M.M[0][3] - M.M[0][1],
			M.M[1][3] - M.M[1][1],
			M.M[2][3] - M.M[2][1],
			M.M[3][3] - M.M[3][1]
		);

		// 6. Bottom 평면: y' >= -w'  <=>  w' + y' >= 0
		Frustum.Planes[Bottom] = FPlane(
			M.M[0][3] + M.M[0][1],
			M.M[1][3] + M.M[1][1],
			M.M[2][3] + M.M[2][1],
			M.M[3][3] + M.M[3][1]
		);

		return Frustum;
	}

	// AABB 박스가 절두체 내부에 있거나 걸쳐있는지 검사
	// 완전히 바깥이면 false (컬링 대상), 일부라도 걸치거나 안쪽이면 true (렌더 대상)
	inline bool Intersects(const FAABB& Box) const
	{
		const FVector Center = (Box.Min + Box.Max) * 0.5f;
		const FVector Extent = (Box.Max - Box.Min) * 0.5f;

		for (int i = 0; i < PlaneCount; ++i)
		{
			const FPlane& Plane = Planes[i];

			// 박스의 반경을 평면 법선에 투영
			const float Radius = Extent.x * std::abs(Plane.Normal.x) +
			                     Extent.y * std::abs(Plane.Normal.y) +
			                     Extent.z * std::abs(Plane.Normal.z);

			// 박스 중심에서 평면까지의 부호 있는 거리
			const float Distance = Plane.Dot(Center);

			// 중심이 평면 바깥쪽으로 반경보다 더 멀리 나가 있으면 완전히 외부에 있는 것임
			if (Distance < -Radius)
			{
				return false; // 컬링!
			}
		}

		return true; // 보임!
	}
};
