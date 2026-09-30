#pragma once

#include "Core.h"
#include "TArray.h"
#include <limits>
#include <stdexcept>

struct FRangePoolBlock
{
	int32 Index = -1;
	int32 Size = 0;

	inline bool IsValid() const { return Index >= 0 && Size > 0; }
};

template <typename T>
class TRangePool
{
private:
	struct FAllocationNode
	{
		FRangePoolBlock Block;
		FAllocationNode* Next = nullptr;
		FAllocationNode* Prev = nullptr;
	};

public:
	TRangePool() = default;
	~TRangePool()
	{
		TraverseNodes(FreeBlockList, [](FAllocationNode* Block) {
			delete Block;
			return true; 
		});
	}

	FRangePoolBlock Allocate(int32 Count)
	{
		FRangePoolBlock AllocatedBlock;

		TraverseNodes(FreeBlockList, [this, Count, &AllocatedBlock](FAllocationNode* Node) {
			if (Node->Block.Size >= Count)
			{
				AllocatedBlock.Index = Node->Block.Index;
				AllocatedBlock.Size = Count;

				Node->Block.Index += Count;
				Node->Block.Size -= Count;

				if (Node->Block.Size == 0)
				{
					// Remove the node from the free list
					if (Node == FreeBlockList)
					{
						FreeBlockList = Node->Next;
					}
					else
					{
						Node->Prev->Next = Node->Next;
					}

					// Update the previous pointer of the next node if it exists
					if (Node->Next)
					{
						Node->Next->Prev = Node->Prev;
					}

					delete Node;
				}

				return false;
			}

			return true;
		});

		if (!AllocatedBlock.IsValid())
		{
			Pool.SetNum(Pool.Num() + Count);
			AllocatedBlock.Index = Pool.Num() - Count;
			AllocatedBlock.Size = Count;
		}

		return AllocatedBlock;
	}

	void Release(const FRangePoolBlock& Block)
	{
		FAllocationNode* Prev = nullptr;
		FAllocationNode* Next = nullptr;
		TraverseNodes(FreeBlockList, [this, &Block, &Prev, &Next](FAllocationNode* Node) {
			if (Node->Block.Index >= Block.Index + Block.Size)
			{
				Next = Node;
				return false;
			}

			Prev = Node;
			return true;
		});
		
		FAllocationNode* Node = nullptr;
		if (Prev && Prev->Block.Index + Prev->Block.Size == Block.Index)
		{
			// Merge with previous block
			Prev->Block.Size += Block.Size;
			Node = Prev;
		}
		else
		{
			// Create a new node for the released block
			Node = new FAllocationNode();
			Node->Block = Block;
			Node->Prev = Prev;
			Node->Next = Next;

			if (Prev)
			{
				Prev->Next = Node;
			}
			else
			{
				FreeBlockList = Node;
			}

			if (Next)
			{
				Next->Prev = Node;
			}
		}

		if (Next && Node->Block.Index + Node->Block.Size == Next->Block.Index)
		{
			// Merge with next block
			Node->Block.Size += Next->Block.Size;
			Node->Next = Next->Next;

			if (Next->Next)
			{
				Next->Next->Prev = Node;
			}

			delete Next;
		}

		// Clear the released block's data
		for (int32 i = 0; i < Block.Size; ++i)
		{
			Pool[Block.Index + i] = T();
		}
	}

	T& Get(const FRangePoolBlock& Block, int32 Offset)
	{
		return Pool[Block.Index + Offset];
	}

	const T& Get(const FRangePoolBlock& Block, int32 Offset) const
	{
		return Pool[Block.Index + Offset];
	}

	inline const TArray<T>& GetPool() const { return Pool; }

private:
	template <typename Func>
	void TraverseNodes(FAllocationNode* Node, Func&& Callback)
	{
		FAllocationNode* Current = Node;
		while (Current)
		{
			FAllocationNode* NextNode = Current->Next;
			if (!Callback(Current))
			{
				break;
			}

			Current = NextNode;
		}
	}

private:
	TArray<T> Pool;
	FAllocationNode* FreeBlockList = nullptr;
};
