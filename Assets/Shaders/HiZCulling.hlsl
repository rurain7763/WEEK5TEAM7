// Hi-Z Occlusion Culling Compute Shader
// Tests scene object AABBs against conservative Hi-Z depth mip pyramid

struct FAABB
{
    float3 Min;
    float3 Max;
};

StructuredBuffer<FAABB> InputAABBs : register(t0);
Texture2D HiZTexture : register(t1);
SamplerState PointClampSampler : register(s0);

RWStructuredBuffer<uint> OutputVisibility : register(u0);

cbuffer CullConstants : register(b0)
{
    row_major matrix ViewProjection;
    float2 HiZResolution;
    uint NumObjects;
    uint MaxMipLevel;
    float DepthBias;
    float NearZ;
    float FarZ;
    float Padding;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= NumObjects)
        return;

    FAABB box = InputAABBs[id.x];

    float3 boxCorners[8] = {
        float3(box.Min.x, box.Min.y, box.Min.z),
        float3(box.Max.x, box.Min.y, box.Min.z),
        float3(box.Min.x, box.Max.y, box.Min.z),
        float3(box.Max.x, box.Max.y, box.Min.z),
        float3(box.Min.x, box.Min.y, box.Max.z),
        float3(box.Max.x, box.Min.y, box.Max.z),
        float3(box.Min.x, box.Max.y, box.Max.z),
        float3(box.Max.x, box.Max.y, box.Max.z)
    };

    float minU = 1.0f;
    float maxU = 0.0f;
    float minV = 1.0f;
    float maxV = 0.0f;
    float minZ = 1.0f;

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float4 clipPos = mul(float4(boxCorners[i], 1.0f), ViewProjection);

        // If any corner is on or behind near plane, conservatively keep visible
        if (clipPos.w <= 0.0001f)
        {
            OutputVisibility[id.x] = 1;
            return;
        }

        float3 ndc = clipPos.xyz / clipPos.w;

        // Convert NDC to UV [0, 1]
        float u = ndc.x * 0.5f + 0.5f;
        float v = -ndc.y * 0.5f + 0.5f;
        float z = ndc.z;

        minU = min(minU, u);
        maxU = max(maxU, u);
        minV = min(minV, v);
        maxV = max(maxV, v);
        minZ = min(minZ, z);
    }

    // If completely outside or crossing screen bounds, keep visible (conservative)
    if (minU < 0.0f || maxU > 1.0f || minV < 0.0f || maxV > 1.0f)
    {
        OutputVisibility[id.x] = 1;
        return;
    }

    // Compute screen space pixel size
    float2 sizeInPixels = float2(maxU - minU, maxV - minV) * HiZResolution;
    float maxDim = max(sizeInPixels.x, sizeInPixels.y);

    // Mip level to sample:
    // If footprint is 2 pixels or smaller, sampling at Mip 0 is exact and avoids bleeding into sky/background texels.
    float lod = (maxDim <= 2.0f) ? 0.0f : clamp(floor(log2(maxDim)), 0.0f, (float)MaxMipLevel);

    // Sample the 4 corners of the bounding box at mip lod
    float d0 = HiZTexture.SampleLevel(PointClampSampler, float2(minU, minV), lod).r;
    float d1 = HiZTexture.SampleLevel(PointClampSampler, float2(maxU, minV), lod).r;
    float d2 = HiZTexture.SampleLevel(PointClampSampler, float2(minU, maxV), lod).r;
    float d3 = HiZTexture.SampleLevel(PointClampSampler, float2(maxU, maxV), lod).r;

    float maxDepth = max(max(d0, d1), max(d2, d3));

    // If maxDepth is 1.0 (clear depth / background sky), nothing in front is occluding it!
    if (maxDepth >= 0.99999f)
    {
        OutputVisibility[id.x] = 1;
        return;
    }

    // Convert DX11 non-linear depth [0, 1] to linear eye depth (meters)
    // Z_linear = (Near * Far) / (Far - Z_ndc * (Far - Near))
    float rangeZ = FarZ - NearZ;
    float denomBox = FarZ - minZ * rangeZ;
    float minLinearZ = (denomBox > 0.0001f) ? (NearZ * FarZ) / denomBox : FarZ;

    float denomHiZ = FarZ - maxDepth * rangeZ;
    float maxLinearZ = (denomHiZ > 0.0001f) ? (NearZ * FarZ) / denomHiZ : FarZ;

    // In linear eye space, DepthBias is in world units (e.g. 0.05 = 5cm).
    // This gives uniform precision whether the camera is 5m or 500m away!
    if (minLinearZ > maxLinearZ + DepthBias)
    {
        OutputVisibility[id.x] = 0; // Occluded
    }
    else
    {
        OutputVisibility[id.x] = 1; // Visible
    }
}
