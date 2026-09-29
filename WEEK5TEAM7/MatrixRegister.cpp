#include "MatrixRegister.h"
#include "Matrix.h"

using namespace VectorSIMD;

FMatrixRegister::FMatrixRegister()
{
	R[0] = VectorSIMD::SetZero();
	R[1] = VectorSIMD::SetZero();
	R[2] = VectorSIMD::SetZero();
	R[3] = VectorSIMD::SetZero();
}

FMatrixRegister::FMatrixRegister(FVectorRegister R0, FVectorRegister R1, FVectorRegister R2, FVectorRegister R3)
{
	R[0] = R0;
	R[1] = R1;
	R[2] = R2;
	R[3] = R3;
}

FMatrixRegister FMatrixRegister::Transpose() const
{
	FMatrixRegister Result = *this;
	VectorSIMD::Transpose(Result.R[0], Result.R[1], Result.R[2], Result.R[3]);
	return Result;
}

float FMatrixRegister::Determinant3x3(FVectorRegister A, FVectorRegister B, FVectorRegister C) const
{
	FVectorRegister Cross = VectorSIMD::Cross3(B, C);
	return VectorSIMD::Dot(A, Cross);
}

float FMatrixRegister::Determinant() const
{
	const FVectorRegister& A = R[0];
	const FVectorRegister& B = R[1];
	const FVectorRegister& C = R[2];
	const FVectorRegister& D = R[3];

	// C00 = +(fgh / jkl / nop)
	FVectorRegister M00 = VectorSIMD::Swizzle<1, 2, 3, 3>(B);
	FVectorRegister M01 = VectorSIMD::Swizzle<1, 2, 3, 3>(C);
	FVectorRegister M02 = VectorSIMD::Swizzle<1, 2, 3, 3>(D);

	float C00 = Determinant3x3(M00, M01, M02);

	// C01 = -(egh / ikl / mop)
	FVectorRegister M10 = VectorSIMD::Shuffle<0, 2, 3, 3>(B, B);
	FVectorRegister M11 = VectorSIMD::Shuffle<0, 2, 3, 3>(C, C);
	FVectorRegister M12 = VectorSIMD::Shuffle<0, 2, 3, 3>(D, D);

	float C01 = -Determinant3x3(M10, M11, M12);

	// C02 = +(efh / ijl / mnp)
	FVectorRegister M20 = VectorSIMD::Shuffle<0, 1, 3, 3>(B, B);
	FVectorRegister M21 = VectorSIMD::Shuffle<0, 1, 3, 3>(C, C);
	FVectorRegister M22 = VectorSIMD::Shuffle<0, 1, 3, 3>(D, D);

	float C02 = Determinant3x3(M20, M21, M22);

	// C03 = -(efg / ijk / mno)
	float C03 = -Determinant3x3(B, C, D);

	// determinant
	return VectorSIMD::Dot(A, VectorSIMD::SetVal(C00, C01, C02, C03));
}


FMatrixRegister FMatrixRegister::Inverse() const
{
    // Cofactor를 4개 lane씩 계산한다. Gauss-Jordan과 달리 분기, pivot 검색,
    // scalar lane 추출이 없고 공통 2x2 minor를 D0~D2에서 재사용한다.
    FVectorRegister Temp1 = _mm_shuffle_ps(R[0], R[1], _MM_SHUFFLE(1, 0, 1, 0));
    FVectorRegister Temp3 = _mm_shuffle_ps(R[0], R[1], _MM_SHUFFLE(3, 2, 3, 2));
    FVectorRegister Temp2 = _mm_shuffle_ps(R[2], R[3], _MM_SHUFFLE(1, 0, 1, 0));
    FVectorRegister Temp4 = _mm_shuffle_ps(R[2], R[3], _MM_SHUFFLE(3, 2, 3, 2));

    FVectorRegister MT0 = _mm_shuffle_ps(Temp1, Temp2, _MM_SHUFFLE(2, 0, 2, 0));
    FVectorRegister MT1 = _mm_shuffle_ps(Temp1, Temp2, _MM_SHUFFLE(3, 1, 3, 1));
    FVectorRegister MT2 = _mm_shuffle_ps(Temp3, Temp4, _MM_SHUFFLE(2, 0, 2, 0));
    FVectorRegister MT3 = _mm_shuffle_ps(Temp3, Temp4, _MM_SHUFFLE(3, 1, 3, 1));

    FVectorRegister V00 = _mm_shuffle_ps(MT2, MT2, _MM_SHUFFLE(1, 1, 0, 0));
    FVectorRegister V10 = _mm_shuffle_ps(MT3, MT3, _MM_SHUFFLE(3, 2, 3, 2));
    FVectorRegister V01 = _mm_shuffle_ps(MT0, MT0, _MM_SHUFFLE(1, 1, 0, 0));
    FVectorRegister V11 = _mm_shuffle_ps(MT1, MT1, _MM_SHUFFLE(3, 2, 3, 2));
    FVectorRegister V02 = _mm_shuffle_ps(MT2, MT0, _MM_SHUFFLE(2, 0, 2, 0));
    FVectorRegister V12 = _mm_shuffle_ps(MT3, MT1, _MM_SHUFFLE(3, 1, 3, 1));

    FVectorRegister D0 = _mm_mul_ps(V00, V10);
    FVectorRegister D1 = _mm_mul_ps(V01, V11);
    FVectorRegister D2 = _mm_mul_ps(V02, V12);

    V00 = _mm_shuffle_ps(MT2, MT2, _MM_SHUFFLE(3, 2, 3, 2));
    V10 = _mm_shuffle_ps(MT3, MT3, _MM_SHUFFLE(1, 1, 0, 0));
    V01 = _mm_shuffle_ps(MT0, MT0, _MM_SHUFFLE(3, 2, 3, 2));
    V11 = _mm_shuffle_ps(MT1, MT1, _MM_SHUFFLE(1, 1, 0, 0));
    V02 = _mm_shuffle_ps(MT2, MT0, _MM_SHUFFLE(3, 1, 3, 1));
    V12 = _mm_shuffle_ps(MT3, MT1, _MM_SHUFFLE(2, 0, 2, 0));

    D0 = _mm_sub_ps(D0, _mm_mul_ps(V00, V10));
    D1 = _mm_sub_ps(D1, _mm_mul_ps(V01, V11));
    D2 = _mm_sub_ps(D2, _mm_mul_ps(V02, V12));

    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 1, 3, 1));
    V00 = _mm_shuffle_ps(MT1, MT1, _MM_SHUFFLE(1, 0, 2, 1));
    V10 = _mm_shuffle_ps(V11, D0, _MM_SHUFFLE(0, 3, 0, 2));
    V01 = _mm_shuffle_ps(MT0, MT0, _MM_SHUFFLE(0, 1, 0, 2));
    V11 = _mm_shuffle_ps(V11, D0, _MM_SHUFFLE(2, 1, 2, 1));

    FVectorRegister V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 3, 3, 1));
    V02 = _mm_shuffle_ps(MT3, MT3, _MM_SHUFFLE(1, 0, 2, 1));
    V12 = _mm_shuffle_ps(V13, D1, _MM_SHUFFLE(0, 3, 0, 2));
    FVectorRegister V03 = _mm_shuffle_ps(MT2, MT2, _MM_SHUFFLE(0, 1, 0, 2));
    V13 = _mm_shuffle_ps(V13, D1, _MM_SHUFFLE(2, 1, 2, 1));

    FVectorRegister C0 = _mm_mul_ps(V00, V10);
    FVectorRegister C2 = _mm_mul_ps(V01, V11);
    FVectorRegister C4 = _mm_mul_ps(V02, V12);
    FVectorRegister C6 = _mm_mul_ps(V03, V13);

    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(0, 0, 1, 0));
    V00 = _mm_shuffle_ps(MT1, MT1, _MM_SHUFFLE(2, 1, 3, 2));
    V10 = _mm_shuffle_ps(D0, V11, _MM_SHUFFLE(2, 1, 0, 3));
    V01 = _mm_shuffle_ps(MT0, MT0, _MM_SHUFFLE(1, 3, 2, 3));
    V11 = _mm_shuffle_ps(D0, V11, _MM_SHUFFLE(0, 2, 1, 2));

    V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(2, 2, 1, 0));
    V02 = _mm_shuffle_ps(MT3, MT3, _MM_SHUFFLE(2, 1, 3, 2));
    V12 = _mm_shuffle_ps(D1, V13, _MM_SHUFFLE(2, 1, 0, 3));
    V03 = _mm_shuffle_ps(MT2, MT2, _MM_SHUFFLE(1, 3, 2, 3));
    V13 = _mm_shuffle_ps(D1, V13, _MM_SHUFFLE(0, 2, 1, 2));

    C0 = _mm_sub_ps(C0, _mm_mul_ps(V00, V10));
    C2 = _mm_sub_ps(C2, _mm_mul_ps(V01, V11));
    C4 = _mm_sub_ps(C4, _mm_mul_ps(V02, V12));
    C6 = _mm_sub_ps(C6, _mm_mul_ps(V03, V13));

    V00 = _mm_shuffle_ps(MT1, MT1, _MM_SHUFFLE(0, 3, 0, 3));
    V10 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 0, 2, 2));
    V10 = _mm_shuffle_ps(V10, V10, _MM_SHUFFLE(0, 2, 3, 0));
    V01 = _mm_shuffle_ps(MT0, MT0, _MM_SHUFFLE(2, 0, 3, 1));
    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 0, 3, 0));
    V11 = _mm_shuffle_ps(V11, V11, _MM_SHUFFLE(2, 1, 0, 3));
    V02 = _mm_shuffle_ps(MT3, MT3, _MM_SHUFFLE(0, 3, 0, 3));
    V12 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 2, 2, 2));
    V12 = _mm_shuffle_ps(V12, V12, _MM_SHUFFLE(0, 2, 3, 0));
    V03 = _mm_shuffle_ps(MT2, MT2, _MM_SHUFFLE(2, 0, 3, 1));
    V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 2, 3, 0));
    V13 = _mm_shuffle_ps(V13, V13, _MM_SHUFFLE(2, 1, 0, 3));

    V00 = _mm_mul_ps(V00, V10);
    V01 = _mm_mul_ps(V01, V11);
    V02 = _mm_mul_ps(V02, V12);
    V03 = _mm_mul_ps(V03, V13);

    FVectorRegister C1 = _mm_sub_ps(C0, V00);
    C0 = _mm_add_ps(C0, V00);
    FVectorRegister C3 = _mm_add_ps(C2, V01);
    C2 = _mm_sub_ps(C2, V01);
    FVectorRegister C5 = _mm_sub_ps(C4, V02);
    C4 = _mm_add_ps(C4, V02);
    FVectorRegister C7 = _mm_add_ps(C6, V03);
    C6 = _mm_sub_ps(C6, V03);

    C0 = _mm_shuffle_ps(C0, C1, _MM_SHUFFLE(3, 1, 2, 0));
    C2 = _mm_shuffle_ps(C2, C3, _MM_SHUFFLE(3, 1, 2, 0));
    C4 = _mm_shuffle_ps(C4, C5, _MM_SHUFFLE(3, 1, 2, 0));
    C6 = _mm_shuffle_ps(C6, C7, _MM_SHUFFLE(3, 1, 2, 0));
    C0 = _mm_shuffle_ps(C0, C0, _MM_SHUFFLE(3, 1, 2, 0));
    C2 = _mm_shuffle_ps(C2, C2, _MM_SHUFFLE(3, 1, 2, 0));
    C4 = _mm_shuffle_ps(C4, C4, _MM_SHUFFLE(3, 1, 2, 0));
    C6 = _mm_shuffle_ps(C6, C6, _MM_SHUFFLE(3, 1, 2, 0));

    const FVectorRegister Determinant = _mm_dp_ps(C0, MT0, 0xFF);
    if (FMath::Abs(_mm_cvtss_f32(Determinant)) < SMALL_NUMBER)
    {
        // FMatrix::Inverse()와 동일한 singular 정책을 유지한다.
        return FMatrixRegister();
    }

    const FVectorRegister InvDeterminant = _mm_div_ps(
        _mm_set1_ps(1.0f),
        Determinant);

    return FMatrixRegister(
        _mm_mul_ps(C0, InvDeterminant),
        _mm_mul_ps(C2, InvDeterminant),
        _mm_mul_ps(C4, InvDeterminant),
        _mm_mul_ps(C6, InvDeterminant));

}

FMatrix FMatrixRegister::ToFMatrix() const
{
	FMatrix Result;

	VectorSIMD::Store(Result.M[0], R[0]);
	VectorSIMD::Store(Result.M[1], R[1]);
	VectorSIMD::Store(Result.M[2], R[2]);
	VectorSIMD::Store(Result.M[3], R[3]);

	return Result;
}

FMatrixRegister FMatrixRegister::Load(const FMatrix& M)
{
	FMatrixRegister Result;

	Result.R[0] = VectorSIMD::Load(M.M[0]);
	Result.R[1] = VectorSIMD::Load(M.M[1]);
	Result.R[2] = VectorSIMD::Load(M.M[2]);
	Result.R[3] = VectorSIMD::Load(M.M[3]);

	return Result;
}

FMatrixRegister FMatrixRegister::Identity()
{
	FMatrixRegister Result(VectorSIMD::SetVal(1, 0, 0, 0), VectorSIMD::SetVal(0, 1, 0, 0), VectorSIMD::SetVal(0, 0, 1, 0), VectorSIMD::SetVal(0, 0, 0, 1));
	return Result;
}
