// Hi-Z Mip 0 Generation: Downsamples depth buffer (R24X8) into R32_FLOAT Hi-Z Mip 0
// Conservative maximum depth reduction (1.0 = Far, 0.0 = Near)

Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> DestMip0 : register(u0);

cbuffer DownsampleInitConstants : register(b0)
{
    uint2 DestSize;   // e.g. (1024, 512)
    uint2 SrcSize;    // e.g. (1920, 1080)
};

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= DestSize.x || id.y >= DestSize.y)
        return;

    // Calculate source UV bounding footprint covered by this Dest pixel
    float2 uv0 = float2(id.xy) / float2(DestSize);
    float2 uv1 = float2(id.xy + 1) / float2(DestSize);

    // Map to source integer pixel coordinates
    int2 srcMin = int2(uv0 * float2(SrcSize));
    int2 srcMax = min(int2(uv1 * float2(SrcSize)), int2(SrcSize) - 1);

    // Sample the 4 corners of the source region
    float d00 = SourceDepth.Load(int3(srcMin.x, srcMin.y, 0)).r;
    float d10 = SourceDepth.Load(int3(srcMax.x, srcMin.y, 0)).r;
    float d01 = SourceDepth.Load(int3(srcMin.x, srcMax.y, 0)).r;
    float d11 = SourceDepth.Load(int3(srcMax.x, srcMax.y, 0)).r;

    // Also sample center
    int2 srcMid = (srcMin + srcMax) / 2;
    float dMid = SourceDepth.Load(int3(srcMid.x, srcMid.y, 0)).r;

    float maxD = max(max(max(d00, d10), max(d01, d11)), dMid);

    DestMip0[id.xy] = maxD;
}
