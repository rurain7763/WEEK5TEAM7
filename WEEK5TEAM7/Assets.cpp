#include "FInstrumentor.h"
#include <chrono>
#include "Assets.h"
#include "FileManager.h"
#include "Stb/stb_image.h"
#include "FLogManager.h"
#include "Renderer.h"
#include "FFontManager.h"
#include "MathUtility.h"
#include "FObjImporter.h"
#include "Serializers.h"
#include "FAssetManager.h"
#include "FTexture2DImporter.h"
#include "AssetFileIOs.h"

namespace
{
void CalculateNormals(FStaticMeshBuildData& MeshData)
{
	for (FVertex& Vertex : MeshData.Vertices)
	{
		Vertex.Normal = FVector(0.0f);
	}

	for (uint32 Index = 0; Index + 2 < static_cast<uint32>(MeshData.Indices.Num()); Index += 3)
	{
		const uint32 Index0 = MeshData.Indices[Index];
		const uint32 Index1 = MeshData.Indices[Index + 1];
		const uint32 Index2 = MeshData.Indices[Index + 2];
		if (Index0 >= static_cast<uint32>(MeshData.Vertices.Num())
			|| Index1 >= static_cast<uint32>(MeshData.Vertices.Num())
			|| Index2 >= static_cast<uint32>(MeshData.Vertices.Num()))
		{
			continue;
		}

		const FVector FaceNormal = FVector::cross(
			MeshData.Vertices[Index1].Pos - MeshData.Vertices[Index0].Pos,
			MeshData.Vertices[Index2].Pos - MeshData.Vertices[Index0].Pos);
		MeshData.Vertices[Index0].Normal += FaceNormal;
		MeshData.Vertices[Index1].Normal += FaceNormal;
		MeshData.Vertices[Index2].Normal += FaceNormal;
	}

	for (FVertex& Vertex : MeshData.Vertices)
	{
		if (Vertex.Normal.LengthSquared() > SMALL_NUMBER)
		{
			Vertex.Normal.Normalize();
		}
		else
		{
			Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
		}
	}
}

FStaticMeshBuildData BuildFromVertices(const FVertex* InVertices, uint32 InVertexCount,
	const uint32* InIndices, uint32 InIndexCount)
{
	FStaticMeshBuildData BuildData;
	BuildData.Vertices.Reserve(InVertexCount);

	for (uint32 Index = 0; Index < InVertexCount; ++Index)
	{
		BuildData.Vertices.Add(InVertices[Index]);
	}

	if (InIndices && InIndexCount > 0)
	{
		BuildData.Indices.Reserve(InIndexCount);
		for (uint32 Index = 0; Index < InIndexCount; ++Index)
		{
			BuildData.Indices.Add(InIndices[Index]);
		}
	}
	else
	{
		BuildData.Indices.Reserve(InVertexCount);
		for (uint32 Index = 0; Index < InVertexCount; ++Index)
		{
			BuildData.Indices.Add(Index);
		}
	}

	FStaticMeshSection& Section = BuildData.Sections.Emplace();
	Section.FirstIndex = 0;
	Section.IndexCount = static_cast<uint32>(BuildData.Indices.Num());
	CalculateNormals(BuildData);
	return BuildData;
}
}

TSharedPtr<FArchive> FFileAssetSource::CreateArchive()
{
	return MakeShared<FWindowsBinReader>(FilePath);
}

FStaticMeshAsset::FStaticMeshAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, const FVertex* InVertices, uint32 InVertexCount)
	: FStaticMeshAsset(InAssetID, InAssetName, InRenderer,
		BuildFromVertices(InVertices, InVertexCount, nullptr, 0))
{
}

FStaticMeshAsset::FStaticMeshAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, const FVertex* InVertices, uint32 InVertexCount, const uint32* InIndices, uint32 InIndexCount)
	: FStaticMeshAsset(InAssetID, InAssetName, InRenderer,
		BuildFromVertices(InVertices, InVertexCount, InIndices, InIndexCount))
{
}

FStaticMeshAsset::FStaticMeshAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, const FStaticMeshBuildData& InBuildData)
	: FAsset(InAssetID, InAssetName, EAssetType::StaticMesh)
	, Vertices(InBuildData.Vertices)
	, Indices(InBuildData.Indices)
	, Sections(InBuildData.Sections)
	, MeshID(NextMeshID++)
{
	if (InBuildData.Vertices.Num() > 0)
	{
		BoundingBox = FAABB(InBuildData.Vertices[0].Pos, InBuildData.Vertices[0].Pos);
		for (int32 i = 1; i < InBuildData.Vertices.Num(); ++i)
		{
			BoundingBox.ExpandToInclude(InBuildData.Vertices[i].Pos);
		}
	}
	
	VertexBuffer = InRenderer.CreateVertexBuffer(InBuildData.Vertices.Data(), static_cast<uint32>(InBuildData.Vertices.Num()));
	IndexBuffer = InRenderer.CreateIndexBuffer(InBuildData.Indices.Data(), static_cast<uint32>(InBuildData.Indices.Num()));
    // 에셋 단위로 생성하며 모든 컴포넌트가 공유합니다.
    if (!RebuildLODs(InRenderer, AppliedLODSettings))
        LocalOctree.Build(Vertices, Indices, BoundingBox, LocalOctreeMaxDepth);
}

bool FStaticMeshAsset::RayCastLocal(const FPickingRay& Ray, float& OutHitT, FMeshOctreeQueryStats* OutStats, float MaxHitT, uint32 LOD) const
{
    return GetLocalOctree(LOD).RayCast(Ray, GetVertices(LOD), GetIndices(LOD), OutHitT, OutStats, MaxHitT);
}

ID3D11Buffer* FStaticMeshAsset::GetVertexBuffer(uint32 LOD) const
{
	const auto* G = GetGeneratedLOD(LOD);
	const auto& Buffer = G ? G->VertexBuffer : VertexBuffer;
	return Buffer ? Buffer->Buffer.Get() : nullptr;
}

uint32 FStaticMeshAsset::GetVertexCount(uint32 LOD) const
{
	return static_cast<uint32>(GetVertices(LOD).Num());
}

ID3D11Buffer * FStaticMeshAsset::GetIndexBuffer(uint32 LOD) const
{
	const auto* G = GetGeneratedLOD(LOD);
    const auto& Buffer = G ? G->IndexBuffer : IndexBuffer;
    return Buffer ? Buffer->Buffer.Get() : nullptr;
}

uint32 FStaticMeshAsset::GetIndexCount(uint32 LOD) const
{
	return static_cast<uint32>(GetIndices(LOD).Num());
}

TSharedPtr<FAsset> FStaticMeshAssetLoader::LoadAsset(const FGuid& AssetID, const FName& AssetName, FArchive& Ar)
{
	FStaticMeshBuildData BuildData;

	FStaticMeshFileIO::Load(Ar, BuildData);

	return MakeShared<FStaticMeshAsset>(AssetID, AssetName, Renderer, BuildData);
}

void FStaticMeshAssetLoader::UnloadAsset(TSharedPtr<FAsset> Asset)
{
	// NOTE: Nothing to do for now
}

TSharedPtr<FAsset> FTexture2DAssetLoader::LoadAsset(const FGuid& AssetID, const FName& AssetName, FArchive& Ar)
{
	int32 Width, Height, Channels;
	TArray<uint8> ImageData;
	
	Ar << Width;
	Ar << Height;
	Ar << Channels;
	Ar << ImageData;

#if 0
	TArray<int8> Memory;
	if (!TryReadToBytes(Ar, Memory))
	{
		UE_LOG_ERROR("Failed to read image data for asset: %s", AssetName.ToString().CStr());
		return nullptr;
	}
	
	stbi_uc* ImageDataPtr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(Memory.Data()), static_cast<int>(Memory.Num()), &Width, &Height, &Channels, 4);
	if (!ImageDataPtr)
	{
		UE_LOG_ERROR("Failed to read image info for asset: %s", AssetName.ToString().CStr());
		return nullptr;
	}

	ImageData.SetNum(Width * Height * 4); // RGBA로 변환
	std::memcpy(ImageData.Data(), ImageDataPtr, ImageData.Num());
	stbi_image_free(ImageDataPtr);
#endif

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = Width;
	TextureDesc.Height = Height;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = 0;
	TextureDesc.MipLevels = 1;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture = Renderer.CreateTexture2D(TextureDesc, ImageData.Data());

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = TextureDesc.Format;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV = Renderer.CreateShaderResourceView(Texture, &SRVDesc);

	return MakeShared<FTexture2DAsset>(AssetID, AssetName, Texture, SRV);
}

void FTexture2DAssetLoader::UnloadAsset(TSharedPtr<FAsset> Asset)
{
	// NOTE: Nothing to do for now
}

TSharedPtr<FAsset> FFontAssetLoader::LoadAsset(const FGuid& AssetID, const FName& AssetName, FArchive& Ar)
{
	TArray<int8> FileData;
	if (!TryReadToBytes(Ar, FileData))
	{
		UE_LOG_ERROR("Failed to read font asset: %s", AssetName.ToString().CStr());
		return nullptr;
	}

	FT_Library Library = FontManager.GetLibrary();

	FT_Face Face;
	FT_Error Err = FT_New_Memory_Face(Library, reinterpret_cast<const FT_Byte*>(FileData.Data()), FileData.Num(), 0, &Face);
	if (Err)
	{
		UE_LOG_ERROR("Failed to load font asset: %s", AssetName.ToString().CStr());
		return nullptr;
	}

	const FT_UInt DefaultSize = 64; // 기본 폰트 크기 설정
	if (FT_Set_Pixel_Sizes(Face, 0, DefaultSize))
	{
		UE_LOG_ERROR("Failed to set font size for asset: %s", AssetName.ToString().CStr());
		FT_Done_Face(Face);
		return nullptr;
	}

	return MakeShared<FFontAsset>(AssetID, AssetName, Face, std::move(FileData));
}

void FFontAssetLoader::UnloadAsset(TSharedPtr<FAsset> Asset)
{
	// Nothing to do for now
}

void FFontAtlasAsset::UpdateRegion(uint32 Left, uint32 Top, uint32 Right, uint32 Bottom, const void* Data, uint32 RowPitch)
{
	if (!Texture || !Data)
	{
		return;
	}

	if (Right <= Left || Bottom <= Top)
	{
		return;
	}

	D3D11_BOX DestBox = {};
	DestBox.left = Left;
	DestBox.top = Top;
	DestBox.right = Right;
	DestBox.bottom = Bottom;
	DestBox.front = 0;
	DestBox.back = 1;

	Renderer.GetDeviceContext()->UpdateSubresource(Texture.Get(), 0, &DestBox, Data, RowPitch, 0);
}

FFontAtlasAsset::FFontAtlasAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, TSharedPtr<FFontAsset>& InFontAsset, uint32 InWidth, uint32 InHeight, uint32 InPaddingW, uint32 InPaddingH)
	: FTexture2DAsset(InAssetID, InAssetName, EAssetType::FontAtlas, nullptr, nullptr)
	, Renderer(InRenderer)
	, FontAsset(InFontAsset)
	, FontAtlas(MakeShared<FFontAtlas>(InFontAsset->GetFace(), InWidth, InHeight, InPaddingW, InPaddingH))
{
	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = InWidth;
	TextureDesc.Height = InHeight;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = 0;

	Texture = Renderer.CreateTexture2D(TextureDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = TextureDesc.Format;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	SRV = Renderer.CreateShaderResourceView(Texture, &SRVDesc);

	Width = InWidth;
	Height = InHeight;
	Format = TextureDesc.Format;

	FontAtlas->SetAtlasHandler(*this);
}

bool FFontAtlasAsset::HandleAddGlyph(FFontAtlas& FontAtlas, const FFontGlyph& InGlyph, const FFontGlyphBitmap& InBitmap)
{
	UpdateRegion(InBitmap.Left, InBitmap.Top, InBitmap.Right, InBitmap.Bottom, InBitmap.Buffer, static_cast<uint32>(InBitmap.Pitch));

	return true;
}

FSpriteAtlasAsset::FSpriteAtlasAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, const TSharedPtr<FTexture2DAsset>& InSource, uint32 InCols, uint32 InRows, uint32 InFrameCount)
	: FTexture2DAsset(InAssetID, InAssetName, EAssetType::SpriteAtlas,
		InSource ? InSource->GetTexture() : Microsoft::WRL::ComPtr<ID3D11Texture2D>(),
		InSource ? InSource->GetSRV() : Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>())
	, Renderer(InRenderer)
{
	if (!InSource)
	{
		UE_LOG_ERROR("Sprite atlas '%s' has no source texture", InAssetName.ToString().CStr());
		return;
	}

	if (InCols == 0 || InRows == 0)
	{
		UE_LOG_ERROR("Sprite atlas '%s' has zero columns or rows", InAssetName.ToString().CStr());
		return;
	}

	const uint32 CellCount = InCols * InRows;
	const uint32 FrameCount = (InFrameCount == 0) ? CellCount : FPlatformMath::Min(InFrameCount, CellCount);

	const float FrameW = 1.0f / static_cast<float>(InCols);
	const float FrameH = 1.0f / static_cast<float>(InRows);

	FrameSubUVs.Reserve(FrameCount);
	for (uint32 i = 0; i < FrameCount; ++i)
	{
		const uint32 Col = i % InCols;
		const uint32 Row = i / InCols;

		FrameSubUVs.Add(FVector4(Col * FrameW, Row * FrameH, FrameW, FrameH));
	}
}

FSpriteAtlasAsset::FSpriteAtlasAsset(const FGuid& InAssetID, const FName& InAssetName, URenderer& InRenderer, const TSharedPtr<FTexture2DAsset>& InSource, const TArray<FVector4>& InFrameSubUVs)
	: FTexture2DAsset(InAssetID, InAssetName, EAssetType::SpriteAtlas,
		InSource ? InSource->GetTexture() : Microsoft::WRL::ComPtr<ID3D11Texture2D>(),
		InSource ? InSource->GetSRV() : Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>())
	, Renderer(InRenderer)
	, FrameSubUVs(InFrameSubUVs)
{
}

const FVector4& FSpriteAtlasAsset::GetFrameSubUV(int32 FrameIndex) const
{
	static const FVector4 WholeTexture(0.f, 0.f, 1.f, 1.f);

	if (FrameSubUVs.IsEmpty())
	{
		return WholeTexture;
	}

	if (FrameIndex < 0 || FrameIndex >= FrameSubUVs.Num())
	{
		return FrameSubUVs[0];
	}

	return FrameSubUVs[static_cast<uint32>(FrameIndex)];
}

FMaterialAsset::FMaterialAsset(const FGuid& InAssetID, const FName& InAssetName, const FVector& InAmbientColor, const FVector& InDiffuseColor, const FVector& InSpecularColor, const FGuid& InDiffuseTexture, const FGuid& InSpecularTexture, const FGuid& InNormalTexture, const float InOpacity)
	: FAsset(InAssetID, InAssetName, EAssetType::Material)
	, AmbientColor(InAmbientColor)
	, DiffuseColor(InDiffuseColor)
	, SpecularColor(InSpecularColor)
	, DiffuseTexture(InDiffuseTexture)
	, SpecularTexture(InSpecularTexture)
	, NormalTexture(InNormalTexture)
	, Opacity(InOpacity)
	, MaterialID(NextMaterialID++)
{
	DiffuseTextureAsset = FAssetManager::Get().GetAssetAs<FTexture2DAsset>(DiffuseTexture, true);
	SpecularTextureAsset = FAssetManager::Get().GetAssetAs<FTexture2DAsset>(SpecularTexture, true);
	NormalTextureAsset = FAssetManager::Get().GetAssetAs<FTexture2DAsset>(NormalTexture, true);
}

uint16 FMaterialAsset::GetPipelineID() const
{
	return Pipeline ? Pipeline->GetPipelineID() : 1;
}

TSharedPtr<FAsset> FMaterialAssetLoader::LoadAsset(const FGuid& AssetID, const FName& AssetName, FArchive& Ar)
{
	
	FMaterialPayload Payload;
	FMaterialFileIO::Load(Ar, Payload);
	
	#if 0
	FVector AmbientColor, DiffuseColor, SpecularColor;
	FGuid DiffuseTexture, SpecularTexture, NormalTexture;
	Ar << AmbientColor;
	Ar << DiffuseColor;
	Ar << Opacity;
	Ar << SpecularColor;
	Ar << DiffuseTexture;
	Ar << SpecularTexture;
	Ar << NormalTexture;
	#endif

	return MakeShared<FMaterialAsset>(AssetID,
									  AssetName,
									  Payload.AmbientColor,
									  Payload.DiffuseColor,
									  Payload.SpecularColor,
									  Payload.DiffuseTexture,
									  Payload.SpecularTexture,
									  Payload.NormalTexture,
									  Payload.Opacity
									  );
}

void FMaterialAssetLoader::UnloadAsset(TSharedPtr<FAsset> Asset)
{
	// NOTE: Nothing to do for now
}






namespace
{
struct FScopedLODTiming
{
    double& Output;
    std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
    ~FScopedLODTiming() { Output = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count(); }
};
}

void FStaticMeshAsset::SetLODSelection(const FMeshLODSelection& Settings)
{
    if (Settings.ForcedLOD < -1 || Settings.ForcedLOD > 2
        || !std::isfinite(Settings.Distances[0]) || !std::isfinite(Settings.Distances[1])
        || Settings.Distances[0] < 0 || Settings.Distances[1] < Settings.Distances[0]) return;
    LODSelection = Settings;
    ++LODChangeVersion;
}

bool FStaticMeshAsset::RebuildLODs(URenderer& Renderer, const FMeshLODSettings& Settings)
{
    PROFILE_SCOPE("Mesh/LODBuild");
    FScopedLODTiming Timing{LastLODBuildMs};
    if (!Settings.IsValid()) { LODError = FString("Invalid LOD settings"); return false; }
    try
    {
        FStaticMeshBuildData Source;
        Source.Vertices = Vertices; Source.Indices = Indices; Source.Sections = Sections;
        TSharedPtr<FGeneratedMeshLOD> Pending[2];
        for (uint32 I = 0; I < 2; ++I)
        {
            Pending[I] = MakeShared<FGeneratedMeshLOD>();
            auto& LOD = *Pending[I];
            if (!BuildSimplifiedMeshLOD(Source, Settings.TriangleRatios[I], Settings.MaxErrors[I], LOD.Data, LOD.SimplificationError))
            {
                LODError = FString("Invalid source mesh or simplification failed"); return false;
            }
            LOD.VertexBuffer = Renderer.CreateVertexBuffer(LOD.Data.Vertices.Data(), LOD.Data.Vertices.Num());
            LOD.IndexBuffer = Renderer.CreateIndexBuffer(LOD.Data.Indices.Data(), LOD.Data.Indices.Num());
            if (!LOD.VertexBuffer || !LOD.IndexBuffer || !LOD.VertexBuffer->Buffer || !LOD.IndexBuffer->Buffer)
            {
                LODError = FString("LOD GPU buffer creation failed"); return false;
            }
            LOD.Octree.Build(LOD.Data.Vertices, LOD.Data.Indices, BoundingBox, Settings.OctreeDepths[I+1]);
            LOD.MeshID = NextMeshID++;
        }
        FMeshPickingOctree PendingRoot;
        PendingRoot.Build(Vertices, Indices, BoundingBox, Settings.OctreeDepths[0]);
        // 준비된 후보만 교체하여 생성 실패 시 기존 메시와 버퍼를 유지합니다.
        LocalOctree = std::move(PendingRoot);
        for (uint32 I = 0; I < 2; ++I) GeneratedLODs[I] = std::move(Pending[I]);
        ++LODResourceVersion;
        ++LODResourceChangeVersion;
        ++LODChangeVersion;
        AppliedLODSettings = Settings;
        LODError = FString();
        return true;
    }
    catch (const std::exception& Error) { LODError = FString(Error.what()); return false; }
}

bool FStaticMeshAsset::RebuildLODOctrees(const FMeshLODSettings& Settings)
{
    PROFILE_SCOPE("Mesh/LODOctreeBuild");
    FScopedLODTiming Timing{LastOctreeBuildMs};
    // 메시 생성 조건은 그대로 두고 깊이만 적용합니다.
    for (int32 Depth : Settings.OctreeDepths)
        if (Depth < 0 || Depth > 16) { LODError = FString("Octree depth must be 0..16"); return false; }
    try
    {
        FMeshPickingOctree Pending[3];
        for (uint32 I = 0; I < 3; ++I)
            if (HasLOD(I)) Pending[I].Build(GetVertices(I), GetIndices(I), BoundingBox, Settings.OctreeDepths[I]);
        LocalOctree = std::move(Pending[0]);
        for (uint32 I = 1; I < 3; ++I)
            if (GeneratedLODs[I-1]) GeneratedLODs[I-1]->Octree = std::move(Pending[I]);
        for (uint32 I = 0; I < 3; ++I) AppliedLODSettings.OctreeDepths[I] = Settings.OctreeDepths[I];
        LODError = FString();
        return true;
    }
    catch (const std::exception& Error) { LODError = FString(Error.what()); return false; }
}
