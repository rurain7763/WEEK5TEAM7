// Hi-Z Mip Reduction: Downsamples ParentMip (k-1) to DestMip (k)
// Conservative maximum depth reduction (1.0 = Far, 0.0 = Near)

Texture2D<float> SourceMip : register(t0);
RWTexture2D<float> DestMip : register(u0);

cbuffer DownsampleMipConstants : register(b0)
{
    uint2 DestSize;
    uint2 SrcSize;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= DestSize.x || id.y >= DestSize.y)
        return;

    int2 srcCoord = id.xy * 2;
    int2 c0 = srcCoord;
    int2 c1 = min(srcCoord + int2(1, 0), int2(SrcSize) - 1);
    int2 c2 = min(srcCoord + int2(0, 1), int2(SrcSize) - 1);
    int2 c3 = min(srcCoord + int2(1, 1), int2(SrcSize) - 1);

    float d0 = SourceMip.Load(int3(c0, 0)).r;
    float d1 = SourceMip.Load(int3(c1, 0)).r;
    float d2 = SourceMip.Load(int3(c2, 0)).r;
    float d3 = SourceMip.Load(int3(c3, 0)).r;

    float maxD = max(max(d0, d1), max(d2, d3));

    DestMip[id.xy] = maxD;
}
