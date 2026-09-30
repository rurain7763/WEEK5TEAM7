#pragma once

#include "Core.h"
#include "FAABB.h"
#include "TArray.h"
#include "TMap.h"
#include <algorithm>

struct FBVHItemRange
{
	int32 Offset = 0;
	int32 Count = 0;
};

struct FBVHNode
{
	FAABB BoundingBox;
	FBVHItemRange ItemRange;
	FBVHNode* Parent = nullptr;
	FBVHNode* Left = nullptr;
	FBVHNode* Right = nullptr;
	// 외부 조회 캐시의 배열 인덱스와 경계/내용 변경 세대입니다.
	uint32 Index = 0;
	uint64 Revision = 1;

	inline bool IsLeaf() const { return Left == nullptr && Right == nullptr; }
};

template <typename T>
class FBVH
{
public:
	FBVH() = default;

	~FBVH()
	{
		Release();
	}

	void AddItem(T Payload, const FAABB& BoundingBox)
	{
		FItemEntry& Entry = Entries.Emplace();
		Entry.Payload = Payload;
		Entry.BoundingBox = BoundingBox;
		Entry.MortonCode = 0;

		FVector Center = (BoundingBox.Min + BoundingBox.Max) * 0.5f;
		if (Entries.Num() == 1)
		{
			CentroidAABB.Min = Center;
			CentroidAABB.Max = Center;
		}
		else
		{
			CentroidAABB.ExpandToInclude(Center);
		}
	}

	void Build()
	{
		if (Entries.Num() == 0)
		{
			return;
		}

		TArray<FBVHNode*> LeafNodes;

		FVector CentroidExtent(
			FMath::Max(CentroidAABB.Max.x - CentroidAABB.Min.x, SMALL_NUMBER),
			FMath::Max(CentroidAABB.Max.y - CentroidAABB.Min.y, SMALL_NUMBER),
			FMath::Max(CentroidAABB.Max.z - CentroidAABB.Min.z, SMALL_NUMBER)
		);
		
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			auto& Entry = Entries[i];

			FVector Center = (Entry.BoundingBox.Min + Entry.BoundingBox.Max) * 0.5f;
			FVector NormalizedCenter = (Center - CentroidAABB.Min) / CentroidExtent;
			
			Entry.MortonCode = GenerateMortonCode(NormalizedCenter.x, NormalizedCenter.y, NormalizedCenter.z);
		}

		std::sort(Entries.begin(), Entries.end(), [](const FItemEntry& A, const FItemEntry& B) { return A.MortonCode < B.MortonCode; });

		LeafNodes.Reserve(Entries.Num());
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			auto& Entry = Entries[i];
			
			FBVHNode* LeafNode = new FBVHNode();
			LeafNode->Index = NodeCount++;
			LeafNode->BoundingBox = Entry.BoundingBox;
			LeafNode->ItemRange.Offset = i;
			LeafNode->ItemRange.Count = 1;
			LeafNodes.Add(LeafNode);

			Entry.Node = LeafNode;

			PayloadToEntryIndexMap.Add(Entry.Payload, i);
		}

		RootNode = BuildImpl(LeafNodes, 0, LeafNodes.Num() - 1);
	}

	void Refit(T Payload, const FAABB& NewBoundingBox)
	{
		if (!PayloadToEntryIndexMap.Contains(Payload))
		{
			return;
		}

		int32 EntryIndex = PayloadToEntryIndexMap[Payload];
		FItemEntry& Entry = Entries[EntryIndex];
		Entry.BoundingBox = NewBoundingBox;

		FBVHNode* Node = Entry.Node;
		Node->BoundingBox = NewBoundingBox;
		++Node->Revision;
		while (Node->Parent)
		{
			Node = Node->Parent;
			++Node->Revision;
			Node->BoundingBox = Node->Left->BoundingBox;
			Node->BoundingBox.ExpandToInclude(Node->Right->BoundingBox.Min);
			Node->BoundingBox.ExpandToInclude(Node->Right->BoundingBox.Max);
		}
	}

	// 경계가 그대로인 외부 상태 변경도 해당 리프와 조상의 조회 캐시만 무효화합니다.
	void Invalidate(T Payload)
	{
		const int32* EntryIndex = PayloadToEntryIndexMap.Find(Payload);
		if (!EntryIndex) return;
		for (FBVHNode* Node = Entries[*EntryIndex].Node; Node; Node = Node->Parent)
			++Node->Revision;
	}

	void Release()
	{
		ReleaseImpl(RootNode);
		PayloadToEntryIndexMap.Empty();
		Entries.Empty();
		CentroidAABB = FAABB();
		RootNode = nullptr;
		NodeCount = 0;
	}

	inline T GetPayload(int32 Index) const
	{
		return Entries[Index].Payload;
	}

	inline FBVHNode* GetRootNode() const { return RootNode; }
	inline uint32 GetNodeCount() const { return NodeCount; }
	inline bool IsValid() const { return RootNode != nullptr; }

private:
	uint32 GenerateMortonCode(float x, float y, float z) const
	{
		uint32 MortonCodeX = static_cast<uint32>(FMath::Clamp(x * MortonCodeScale, 0.0f, static_cast<float>(MortonCodeScale - 1)));
		uint32 MortonCodeY = static_cast<uint32>(FMath::Clamp(y * MortonCodeScale, 0.0f, static_cast<float>(MortonCodeScale - 1)));
		uint32 MortonCodeZ = static_cast<uint32>(FMath::Clamp(z * MortonCodeScale, 0.0f, static_cast<float>(MortonCodeScale - 1)));

		uint32 MortonCode = 0;
		for (uint32 i = 0; i < MortonCodeBits; ++i)
		{
			uint32 BitMask = 1 << i;
			uint32 XBit = (MortonCodeX & BitMask) >> i;
			uint32 YBit = (MortonCodeY & BitMask) >> i;
			uint32 ZBit = (MortonCodeZ & BitMask) >> i;

			MortonCode |= (XBit << (3 * i)) | (YBit << (3 * i + 1)) | (ZBit << (3 * i + 2));
		}

		return MortonCode;
	}

	FBVHNode* BuildImpl(TArray<FBVHNode*>& Nodes, int32 StartIndex, int32 EndIndex)
	{
		if (StartIndex > EndIndex)
		{
			return nullptr;
		}

		if (StartIndex == EndIndex)
		{
			return Nodes[StartIndex];
		}

		int32 MidIndex = (StartIndex + EndIndex) / 2;

		FBVHNode* LeftNode = BuildImpl(Nodes, StartIndex, MidIndex);
		FBVHNode* RightNode = BuildImpl(Nodes, MidIndex + 1, EndIndex);

		FBVHNode* NewNode = new FBVHNode();
		NewNode->Index = NodeCount++;
		NewNode->BoundingBox = LeftNode->BoundingBox;
		NewNode->BoundingBox.ExpandToInclude(RightNode->BoundingBox.Min);
		NewNode->BoundingBox.ExpandToInclude(RightNode->BoundingBox.Max);
		NewNode->ItemRange.Offset = LeftNode->ItemRange.Offset;
		NewNode->ItemRange.Count = LeftNode->ItemRange.Count + RightNode->ItemRange.Count;
		NewNode->Left = LeftNode;
		NewNode->Right = RightNode;

		LeftNode->Parent = NewNode;
		RightNode->Parent = NewNode;

		return NewNode;
	}

	void ReleaseImpl(FBVHNode* Node)
	{
		if (!Node)
		{
			return;
		}

		ReleaseImpl(Node->Left);
		ReleaseImpl(Node->Right);

		delete Node;
	}

private:
	constexpr static uint32 MortonCodeBits = 10; // 10bit
	constexpr static uint32 MortonCodeScale = 1 << MortonCodeBits; // 10bit

	struct FItemEntry
	{
		T Payload;
		FAABB BoundingBox;
		uint32 MortonCode;
		FBVHNode* Node = nullptr;
	};

	TMap<T, int32> PayloadToEntryIndexMap;
	TArray<FItemEntry> Entries;
	FAABB CentroidAABB;
	FBVHNode* RootNode = nullptr;
	uint32 NodeCount = 0;
};
