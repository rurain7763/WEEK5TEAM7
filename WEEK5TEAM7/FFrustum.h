#pragma once

#include "Core.h"
#include "Vector.h"
#include "Matrix.h"
#include "FAABB.h"
#include "VectorRegister.h"

struct FPlane
{
	FVector Normal = FVector(0.f, 0.f, 0.f);
	float Distance = 0.0f;
	FVector AbsNormal = FVector(0.f, 0.f, 0.f);

	inline bool IsPointInFront(const FVector& Point) const
	{
		return FVector::dot(Normal, Point) + Distance >= 0;
	}

	inline float DistanceToPoint(const FVector& Point) const
	{
		return FVector::dot(Normal, Point) + Distance;
	}

	inline void Normalize()
	{
		float Length = Normal.Length();
		if (Length > SMALL_NUMBER)
		{
			Normal /= Length;
			Distance /= Length;
			AbsNormal = FVector(FGenericPlatformMath::Abs(Normal.x), FGenericPlatformMath::Abs(Normal.y), FGenericPlatformMath::Abs(Normal.z));
		}
	}

	static FPlane Create(const FVector& A, const FVector& B, const FVector& C)
	{
		FPlane Plane;
		Plane.Normal = FVector::cross(B - A, C - A);
		Plane.Normal.Normalize();
		Plane.Distance = -FVector::dot(Plane.Normal, A);
		Plane.AbsNormal = FVector(FGenericPlatformMath::Abs(Plane.Normal.x), FGenericPlatformMath::Abs(Plane.Normal.y), FGenericPlatformMath::Abs(Plane.Normal.z));
		return Plane;
	}
};

struct FFrustumCorners
{
	FVector Corners[8]{};

	FVector& NTL() { return Corners[0]; }
	const FVector& NTL() const { return Corners[0]; }

	FVector& NTR() { return Corners[1]; }
	const FVector& NTR() const { return Corners[1]; }

	FVector& NBL() { return Corners[2]; }
	const FVector& NBL() const { return Corners[2]; }

	FVector& NBR() { return Corners[3]; }
	const FVector& NBR() const { return Corners[3]; }

	FVector& FTL() { return Corners[4]; }
	const FVector& FTL() const { return Corners[4]; }

	FVector& FTR() { return Corners[5]; }
	const FVector& FTR() const { return Corners[5]; }

	FVector& FBL() { return Corners[6]; }
	const FVector& FBL() const { return Corners[6]; }

	FVector& FBR() { return Corners[7]; }
	const FVector& FBR() const { return Corners[7]; }

	static FFrustumCorners Create(const FMatrix& InvViewProjection)
	{
		FFrustumCorners FrustumCorners;

		// Define the normalized device coordinates for the frustum corners
		FVector NDC[8] = {
			FVector(-1,  1, 0), // NTL
			FVector(1,  1, 0), // NTR
			FVector(-1, -1, 0), // NBL
			FVector(1, -1, 0), // NBR
			FVector(-1,  1, 1), // FTL
			FVector(1,  1, 1), // FTR
			FVector(-1, -1, 1), // FBL
			FVector(1, -1, 1)  // FBR
		};

		for (int32 i = 0; i < 8; ++i)
		{
			FVector4 CornerH = FVector4(NDC[i], 1.0f) * InvViewProjection;
			FrustumCorners.Corners[i] = FVector(CornerH.x / CornerH.w, CornerH.y / CornerH.w, CornerH.z / CornerH.w);
		}
		
		return FrustumCorners;
	}
};

struct FFrustumPlanePacket
{
	FVectorRegister NormalX;
	FVectorRegister NormalY;
	FVectorRegister NormalZ;
	FVectorRegister Distance;

	FVectorRegister AbsNormalX;
	FVectorRegister AbsNormalY;
	FVectorRegister AbsNormalZ;
};

struct FFrustum
{
	FPlane Planes[6]{};

	FFrustumPlanePacket PlanePackets[2]{};

	FPlane& Left() { return Planes[0]; }
	const FPlane& Left() const { return Planes[0]; }

	FPlane& Right() { return Planes[1]; }
	const FPlane& Right() const { return Planes[1]; }

	FPlane& Top() { return Planes[2]; }
	const FPlane& Top() const { return Planes[2]; }

	FPlane& Bottom() { return Planes[3]; }
	const FPlane& Bottom() const { return Planes[3]; }

	FPlane& Near() { return Planes[4]; }
	const FPlane& Near() const { return Planes[4]; }

	FPlane& Far() { return Planes[5]; }
	const FPlane& Far() const { return Planes[5]; }

	// NOTE: -1 = Outside, 0 = Intersecting, 1 = Inside
	int32 Intersects(const FAABB& BoundingBox) const
	{
#if 0
		const FVector Center = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
		const FVector Extent = (BoundingBox.Max - BoundingBox.Min) * 0.5f;
		bool bIntersecting = false;

		for (int i = 0; i < 6; ++i)
		{
			const FPlane& Plane = Planes[i];

			// 박스의 반경을 평면 법선에 투영
			const float Radius = Extent.x * Plane.AbsNormal.x + Extent.y * Plane.AbsNormal.y + Extent.z * Plane.AbsNormal.z;
			const float Distance = Plane.DistanceToPoint(Center);

			if (Distance < -Radius)
			{
				return -1;
			}

			if (Distance < Radius)
			{
				bIntersecting = true;
			}
		}

		return bIntersecting ? 0 : 1;
#else
		const FVector Center = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
		const FVector Extent = (BoundingBox.Max - BoundingBox.Min) * 0.5f;
		bool bIntersecting = false;

		const FVectorRegister CenterX = VectorSIMD::SetVal(Center.x);
		const FVectorRegister CenterY = VectorSIMD::SetVal(Center.y);
		const FVectorRegister CenterZ = VectorSIMD::SetVal(Center.z);
		const FVectorRegister ExtentX = VectorSIMD::SetVal(Extent.x);
		const FVectorRegister ExtentY = VectorSIMD::SetVal(Extent.y);
		const FVectorRegister ExtentZ = VectorSIMD::SetVal(Extent.z);
		const FVectorRegister Zero = VectorSIMD::SetZero();

		for (int32 PacketIndex = 0; PacketIndex < 2; ++PacketIndex)
		{
			const FFrustumPlanePacket& Packet = PlanePackets[PacketIndex];

			FVectorRegister Distance = VectorSIMD::MultiplyAdd(Packet.NormalX, CenterX, Packet.Distance);
			Distance = VectorSIMD::MultiplyAdd(Packet.NormalY, CenterY, Distance);
			Distance = VectorSIMD::MultiplyAdd(Packet.NormalZ, CenterZ, Distance);

			FVectorRegister Radius = VectorSIMD::Mul(Packet.AbsNormalX, ExtentX);
			Radius = VectorSIMD::MultiplyAdd(Packet.AbsNormalY, ExtentY, Radius);
			Radius = VectorSIMD::MultiplyAdd(Packet.AbsNormalZ, ExtentZ, Radius);

			// 첫 패킷은 4개 평면, 두 번째 패킷은 Near/Far 2개 평면만 유효하다.
			const int32 ValidMask = PacketIndex == 0 ? 0xF : 0x3;
			const FVectorRegister NegativeRadius = VectorSIMD::Sub(Zero, Radius);
			const int32 OutsideMask = _mm_movemask_ps(_mm_cmplt_ps(Distance, NegativeRadius)) & ValidMask;

			if (OutsideMask != 0)
			{
				return -1;
			}

			const int32 IntersectingMask = _mm_movemask_ps(_mm_cmplt_ps(Distance, Radius)) & ValidMask;
			if (IntersectingMask != 0)
			{
				bIntersecting = true;
			}
		}

		return bIntersecting ? 0 : 1;
#endif
	}

	static FFrustum Create(const FMatrix& ViewProjection)
	{
		FFrustum Frustum;

		// Near plane
		FPlane& Near = Frustum.Near();
		Near.Normal.x = ViewProjection.M[0][2];
		Near.Normal.y = ViewProjection.M[1][2];
		Near.Normal.z = ViewProjection.M[2][2];
		Near.Distance = ViewProjection.M[3][2];
		Near.Normalize();

		// Far plane
		FPlane& Far = Frustum.Far();
		Far.Normal.x = ViewProjection.M[0][3] - ViewProjection.M[0][2];
		Far.Normal.y = ViewProjection.M[1][3] - ViewProjection.M[1][2];
		Far.Normal.z = ViewProjection.M[2][3] - ViewProjection.M[2][2];
		Far.Distance = ViewProjection.M[3][3] - ViewProjection.M[3][2];
		Far.Normalize();
		
		// Left plane
		FPlane& Left = Frustum.Left();
		Left.Normal.x = ViewProjection.M[0][3] + ViewProjection.M[0][0];
		Left.Normal.y = ViewProjection.M[1][3] + ViewProjection.M[1][0];
		Left.Normal.z = ViewProjection.M[2][3] + ViewProjection.M[2][0];
		Left.Distance = ViewProjection.M[3][3] + ViewProjection.M[3][0];
		Left.Normalize();
		
		// Right plane
		FPlane& Right = Frustum.Right();
		Right.Normal.x = ViewProjection.M[0][3] - ViewProjection.M[0][0];
		Right.Normal.y = ViewProjection.M[1][3] - ViewProjection.M[1][0];
		Right.Normal.z = ViewProjection.M[2][3] - ViewProjection.M[2][0];
		Right.Distance = ViewProjection.M[3][3] - ViewProjection.M[3][0];
		Right.Normalize();
		
		// Top plane
		FPlane& Top = Frustum.Top();
		Top.Normal.x = ViewProjection.M[0][3] - ViewProjection.M[0][1];
		Top.Normal.y = ViewProjection.M[1][3] - ViewProjection.M[1][1];
		Top.Normal.z = ViewProjection.M[2][3] - ViewProjection.M[2][1];
		Top.Distance = ViewProjection.M[3][3] - ViewProjection.M[3][1];
		Top.Normalize();
		
		// Bottom plane
		FPlane& Bottom = Frustum.Bottom();
		Bottom.Normal.x = ViewProjection.M[0][3] + ViewProjection.M[0][1];
		Bottom.Normal.y = ViewProjection.M[1][3] + ViewProjection.M[1][1];
		Bottom.Normal.z = ViewProjection.M[2][3] + ViewProjection.M[2][1];
		Bottom.Distance = ViewProjection.M[3][3] + ViewProjection.M[3][1];
		Bottom.Normalize();
		
		Frustum.BuildSIMDPackets();
		return Frustum;
	}

	static FFrustum Create(const FFrustumCorners& Corners)
	{
		FFrustum Frustum;

		// Near Plane
		Frustum.Near() = FPlane::Create(Corners.NTL(), Corners.NBL(), Corners.NBR());

		// Far Plane
		Frustum.Far() = FPlane::Create(Corners.FTR(), Corners.FBR(), Corners.FTL());

		// Left plane
		Frustum.Left() = FPlane::Create(Corners.NTL(), Corners.FTL(), Corners.NBL());

		// Right plane
		Frustum.Right() = FPlane::Create(Corners.NBR(), Corners.FBR(), Corners.NTR());

		// Top plane
		Frustum.Top() = FPlane::Create(Corners.NTL(), Corners.NTR(), Corners.FTL());

		// Bottom plane
		Frustum.Bottom() = FPlane::Create(Corners.NBR(), Corners.NBL(), Corners.FBR());

		Frustum.BuildSIMDPackets();
		return Frustum;
	}

	void BuildSIMDPackets()
	{
		for (int PacketIndex = 0; PacketIndex < 2; ++PacketIndex)
		{
			const int Base = PacketIndex * 4;

			float Nx[4]{};
			float Ny[4]{};
			float Nz[4]{};
			float D[4]{};
			float Ax[4]{};
			float Ay[4]{};
			float Az[4]{};

			for (int Lane = 0; Lane < 4; ++Lane)
			{
				const int PlaneIndex = Base + Lane;

				if (PlaneIndex >= 6)
				{
					continue;
				}

				const FPlane& Plane = Planes[PlaneIndex];

				Nx[Lane] = Plane.Normal.x;
				Ny[Lane] = Plane.Normal.y;
				Nz[Lane] = Plane.Normal.z;
				D[Lane] = Plane.Distance;

				Ax[Lane] = Plane.AbsNormal.x;
				Ay[Lane] = Plane.AbsNormal.y;
				Az[Lane] = Plane.AbsNormal.z;
			}

			FFrustumPlanePacket& Packet = PlanePackets[PacketIndex];

			Packet.NormalX = VectorSIMD::Load(Nx);
			Packet.NormalY = VectorSIMD::Load(Ny);
			Packet.NormalZ = VectorSIMD::Load(Nz);
			Packet.Distance = VectorSIMD::Load(D);
			Packet.AbsNormalX = VectorSIMD::Load(Ax);
			Packet.AbsNormalY = VectorSIMD::Load(Ay);
			Packet.AbsNormalZ = VectorSIMD::Load(Az);
		}
	}
};
