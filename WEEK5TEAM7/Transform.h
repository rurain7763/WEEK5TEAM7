#pragma once
#include "Vector.h"
#include "Rotator.h"
#include "Matrix.h"
#include <cassert>

// 스케일 하한. 0에 가까워지면 MakeMatrix()의 행렬식(세 축 스케일의 곱)이 무너져
// FMatrix::Inverse()가 Identity를 돌려주고, 그 액터는 레이캐스트로 클릭할 수 없게 된다.
// SMALL_NUMBER는 부동소수점 오차를 재는 값이라 물리적 크기의 하한으로는 너무 작다.
constexpr float MIN_SCALE = 0.001f;

struct FTransform
{
public:
	FTransform(){ }
	// 값 복사는 변경 알림의 소유자를 복사하지 않습니다.
	FTransform(const FTransform& Other)
		: Location(Other.Location), Rotation(Other.Rotation), Scale(Other.Scale) {}
	FTransform& operator=(const FTransform& Other)
	{
		if (this != &Other)
		{
            if (Location == Other.Location && Rotation == Other.Rotation && Scale == Other.Scale)
                return *this;
            Location = Other.Location;
            Rotation = Other.Rotation;
            Scale = Other.Scale;
            MarkChanged();
		}
		return *this;
	}
	void SetChangeCallback(void* Context, void (*Callback)(void*))
	{
		ChangeContext = Context;
		OnChanged = Callback;
	}
	FTransform(FVector _Location, FRotator _Rotation, FVector _Scale) : Location(_Location), Rotation(_Rotation), Scale(_Scale)
	{
	}
	
	inline const FMatrix& MakeMatrix() const
	{
		EnsureUpdateTransformMatrix();

		return mTransformMatrix;
	}

	const FMatrix& InverseMatrix() const
	{
		if (mbInverseTransformDirty)
		{
			EnsureUpdateTransformMatrix();
			mInverseTransformMatrix = mTransformMatrix.AffineInverse();
			mbInverseTransformDirty = false;
		}

		return mInverseTransformMatrix;
	}

	inline void SetLocation(const FVector& InLocation) 
	{ 
		if (Location == InLocation)
		{
			return;
		}

		Location = InLocation; 
		MarkChanged();
	}

	inline FVector GetLocation() const { return Location; }

	inline void SetRotation(const FRotator& InRotation) 
	{ 
		if (Rotation == InRotation)
		{
			return;
		}

		Rotation = InRotation; 
		MarkChanged();
	}

	inline FRotator GetRotation() const { return Rotation; }
	
	inline void SetScale(const FVector& InScale)
	{ 
		if (Scale == InScale)
		{
			return;
		}

		Scale = InScale; 
		MarkChanged();
	}

	inline FVector GetScale() const { return Scale; }

	inline uint32 GetTransformVersion() const { return TransformVersion; }

private:
    // 행렬 캐시, 버전, 외부 알림을 한 곳에서 갱신합니다.
    void MarkChanged()
    {
        mbTransformDirty = true;
        mbInverseTransformDirty = true;
        ++TransformVersion;
        if (OnChanged) OnChanged(ChangeContext);
    }

	void EnsureUpdateTransformMatrix() const
	{
		if (!mbTransformDirty)
		{
			return;
		}
		
		mTransformMatrix = FMatrix::Scale(Scale) * FMatrix::Rotate(Rotation) * FMatrix::Translation(Location);
		mbTransformDirty = false;
	}

private:
	FVector Location = FVector(0);
	FRotator Rotation = FRotator(0, 0, 0);
	FVector Scale = FVector(1);
	uint32 TransformVersion = 1;
	void* ChangeContext = nullptr;
	void (*OnChanged)(void*) = nullptr;

	mutable bool mbTransformDirty = true;
	mutable FMatrix mTransformMatrix;
	mutable bool mbInverseTransformDirty = true;
	mutable FMatrix mInverseTransformMatrix;
};
