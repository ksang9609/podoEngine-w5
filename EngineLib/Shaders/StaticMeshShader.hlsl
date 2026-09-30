
struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

struct PS_INPUT
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

Texture2D g_txColor : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D SpecularTexture : register(t2);
SamplerState g_Sample : register(s0);

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 ViewProjection;
}

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 World;
    float4 Tint;
    // sub uv
    float2 UVScale;
    float2 UVOffset;
    float2 Pad;
}

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    output.position = mul(mul(float4(input.position.xyz, 1.0f), World), ViewProjection);
    output.color = input.color;
    output.uv = input.uv * UVScale + UVOffset;

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 sampleColor = g_txColor.Sample(g_Sample, input.uv) * input.color;
    float3 smapleColorRGB = sampleColor.rgb * lerp(1, Tint.rgb, Tint.a);
    return float4(smapleColorRGB, sampleColor.a);
}
