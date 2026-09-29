#include "FMeshLODBuilder.h"
#include "ThirdParty/meshoptimizer/meshoptimizer.h"
#include <algorithm>

bool BuildSimplifiedMeshLOD(const FStaticMeshBuildData& Source, float Ratio, float MaxError,
    FStaticMeshBuildData& Out, float& OutError)
{
    if (!std::isfinite(Ratio) || Ratio <= 0 || Ratio > 1 || !std::isfinite(MaxError)
        || MaxError < 0 || MaxError > 1 || Source.Vertices.IsEmpty()
        || Source.Indices.IsEmpty() || Source.Indices.Num() % 3 || Source.Sections.IsEmpty()) return false;
    for (uint32 Index : Source.Indices)
        if (Index >= static_cast<uint32>(Source.Vertices.Num())) return false;

    struct FAttributes { float Values[9]; };
    TArray<FAttributes> Attributes;
    Attributes.Reserve(Source.Vertices.Num());
    for (const FVertex& V : Source.Vertices)
    {
        const FAttributes A = {{ V.Normal.x, V.Normal.y, V.Normal.z, V.Tex.X, V.Tex.Y,
            V.Color.x, V.Color.y, V.Color.z, V.Color.w }};
        if (!std::isfinite(V.Pos.x) || !std::isfinite(V.Pos.y) || !std::isfinite(V.Pos.z)) return false;
        for (float Value : A.Values) if (!std::isfinite(Value)) return false;
        Attributes.Add(A);
    }
    const float Weights[9] = { .5f, .5f, .5f, 1.f, 1.f, .25f, .25f, .25f, .25f };
    FStaticMeshBuildData Result;
    float Error = 0;
    uint32 ExpectedFirst = 0;
    for (const FStaticMeshSection& Section : Source.Sections)
    {
        // 재질 섹션별로 분리하여 경계를 잠그고 슬롯 순서를 유지합니다.
        if (Section.FirstIndex != ExpectedFirst || Section.IndexCount % 3
            || static_cast<uint64>(Section.FirstIndex) + Section.IndexCount > static_cast<uint64>(Source.Indices.Num())) return false;
        ExpectedFirst += Section.IndexCount;
        FStaticMeshSection NewSection = Section;
        NewSection.FirstIndex = static_cast<uint32>(Result.Indices.Num());
        TArray<uint32> Simplified;
        Simplified.SetNum(Section.IndexCount);
        if (Section.IndexCount > 0)
        {
            const size_t Target = (std::max)(size_t{3}, static_cast<size_t>(Section.IndexCount / 3 * Ratio) * 3);
            float SectionError = 0;
            // 노멀·UV 불연속도 오차 범위 내에서 축약하되 재질 섹션의 열린 경계는 유지합니다.
            // 불연속을 모두 고정하면 사과처럼 정점이 분리된 메시에서 축약이 멈출 수 있습니다.
            size_t Count = meshopt_simplifyWithAttributes(Simplified.Data(), Source.Indices.Data() + Section.FirstIndex,
                Section.IndexCount, &Source.Vertices[0].Pos.x, Source.Vertices.Num(), sizeof(FVertex),
                Attributes[0].Values, sizeof(FAttributes), Weights, 9, nullptr, Target, MaxError,
                meshopt_SimplifyLockBorder | meshopt_SimplifyPermissive, &SectionError);
            // 사라진 섹션은 원본으로 유지합니다. 빈 GPU 버퍼를 만들지 않습니다.
            if (Count == 0)
            {
                Count = Section.IndexCount;
                std::copy_n(Source.Indices.Data() + Section.FirstIndex, Count, Simplified.Data());
                SectionError = 0;
            }
            Simplified.SetNum(static_cast<int32>(Count));
            Error = (std::max)(Error, SectionError);
        }
        NewSection.IndexCount = static_cast<uint32>(Simplified.Num());
        for (uint32 Index : Simplified) Result.Indices.Add(Index);
        Result.Sections.Add(NewSection);
    }
    if (ExpectedFirst != static_cast<uint32>(Source.Indices.Num())) return false;

    // 사용하지 않는 정점을 제거하고 인덱스를 첫 사용 순서로 다시 연결합니다.
    TArray<uint32> Remap;
    Remap.Init(~uint32{0}, Source.Vertices.Num());
    for (uint32& Index : Result.Indices)
    {
        if (Remap[Index] == ~uint32{0}) Remap[Index] = Result.Vertices.Add(Source.Vertices[Index]);
        Index = Remap[Index];
    }
    Out = std::move(Result);
    OutError = Error;
    return true;
}
