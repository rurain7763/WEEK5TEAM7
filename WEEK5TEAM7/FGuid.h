#pragma once

#include "Core.h"
#include "Json/json.hpp"
#include <random>
#include <format>

struct FGuid
{
	uint32 A;
	uint32 B;
	uint32 C;
	uint32 D;

	FGuid() : A(0), B(0), C(0), D(0) {}
	FGuid(uint32 InA, uint32 InB, uint32 InC, uint32 InD) : A(InA), B(InB), C(InC), D(InD) {}

	inline bool IsValid() const
	{
		return A != 0 || B != 0 || C != 0 || D != 0;
	}

	bool operator==(const FGuid& Other) const
	{
		return A == Other.A && B == Other.B && C == Other.C && D == Other.D;
	}

	bool operator!=(const FGuid& Other) const
	{
		return !(*this == Other);
	}

	FString ToString() const
	{
		return std::format("%d-%d-%d-%d", A, B, C, D);
	}
	
	static FGuid NewGuid();
};

template <>
struct std::hash<FGuid>
{
	std::size_t operator()(const FGuid& Guid) const noexcept
	{
		const uint64 Lo = (uint64(Guid.A) << 32) | uint64(Guid.B);
		const uint64 Hi = (uint64(Guid.C) << 32) | uint64(Guid.D);

		// 두 절반을 각각 혼합한 뒤 결합한다.
		// 단순 Lo ^ Hi는 두 절반이 같으면 항상 0이 된다.
		uint64 X = Lo * 0x9E3779B185EBCA87ULL;
		uint64 Y = Hi * 0xC2B2AE3D27D4EB4FULL;
		uint64 H = X ^ ((Y << 31) | (Y >> 33));

		// 상위 비트의 변화도 하위 비트로 퍼뜨린다.
		H ^= H >> 33;
		H *= 0xFF51AFD7ED558CCDULL;
		H ^= H >> 33;
		H *= 0xC4CEB9FE1A85EC53ULL;
		H ^= H >> 33;

		return static_cast<std::size_t>(H);
	}
};