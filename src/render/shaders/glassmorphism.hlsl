// ==============================================================================
// HLSL Shaders: glassmorphism.hlsl
// Separable Gaussian Blur & Glassmorphism Material Composite
// Derived from REA Investigation of DWMBlurGlass CustomBlur
// ==============================================================================

// Constant buffer for separable Gaussian blur passes (Pass 1 & Pass 2)
cbuffer GaussianBlurBuffer : register(b0)
{
    float2 u_blurDir;           // Direction in UV units (1.0 / targetW, 0.0) or (0.0, 1.0 / targetH)
    float  u_tapCount;          // Number of bilinear tap pairs (e.g. 8.0)
    float  u_centerWeight;      // Center sample weight w0
    float4 u_taps[10];          // .x = offset in pixels, .y = normalized weight, .zw = 0
};

// Constant buffer for final glassmorphism composite pass (Pass 3)
cbuffer GlassmorphismBuffer : register(b0)
{
    float2 u_cardSize;          // Physical card dimensions (width, height)
    float2 u_margin;            // Backdrop sampling margin (e.g. 48.0, 48.0)
    float4 u_cornerRadii;       // (topLeft, topRight, bottomRight, bottomLeft)
    float  u_blurRadius;        // Diffusion blur radius in pixels (~36.0 px * CARD_SCALE)
    float  u_tintOpacity;       // Neutral dark charcoal surface tint opacity (~0.60)
    float  u_borderOpacity;     // Subtle boundary stroke opacity (~0.22)
    float  u_highlightStrength; // Broad surface highlight intensity (~0.04)
    float4 u_tintColor;         // Base dark charcoal surface tint (0.108, 0.110, 0.122, 1.0)
    float4 u_padding;           // 16-byte alignment padding
};

Texture2D    InputTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct VS_OUTPUT
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

// Fullscreen Triangle Vertex Shader (zero VBO / zero input layout)
VS_OUTPUT VSMain(uint vertexId : SV_VertexID)
{
    VS_OUTPUT output;
    output.TexCoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(output.TexCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// ------------------------------------------------------------------------------
// Separable 1D Gaussian Blur Pixel Shader (Used for Pass 1 Horiz & Pass 2 Vert)
// Bilinear tap paired Gaussian sampling with exp(-r^2 / (2 * sigma^2))
// ------------------------------------------------------------------------------

float4 PS_GaussianBlur(VS_OUTPUT input) : SV_Target
{
    float4 sum = InputTexture.Sample(LinearSampler, input.TexCoord) * u_centerWeight;
    int count = int(u_tapCount);

    [unroll]
    for (int i = 0; i < 10; ++i)
    {
        if (i >= count) break;
        float2 off = u_blurDir * u_taps[i].x;
        float w = u_taps[i].y;
        sum += InputTexture.Sample(LinearSampler, input.TexCoord + off) * w;
        sum += InputTexture.Sample(LinearSampler, input.TexCoord - off) * w;
    }

    return sum;
}

// ------------------------------------------------------------------------------
// Geometry & Signed Distance Fields for Rounded Rectangle
// ------------------------------------------------------------------------------

float radiusAt(float2 coord, float4 radii)
{
    if (coord.x >= 0.0) {
        if (coord.y <= 0.0) return radii.y;
        else return radii.z;
    } else {
        if (coord.y <= 0.0) return radii.x;
        else return radii.w;
    }
}

float sdRoundedRect(float2 coord, float2 halfSize, float radius)
{
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius, radius));
    float outside = length(max(cornerCoord, 0.0)) - radius;
    float inside = min(max(cornerCoord.x, cornerCoord.y), 0.0);
    return outside + inside;
}

// ------------------------------------------------------------------------------
// Main Fragment Shader (Pass 3: Composite, Upsample, Tint, Highlight, Border)
// ------------------------------------------------------------------------------

float4 PSMain(VS_OUTPUT input) : SV_Target
{
    float2 coord = input.TexCoord * u_cardSize;
    float2 halfSize = u_cardSize * 0.5;
    float2 centeredCoord = coord - halfSize;
    float radius = radiusAt(centeredCoord, u_cornerRadii);

    // 1. Evaluate Signed Distance Field
    float sd = sdRoundedRect(centeredCoord, halfSize, radius);

    // Anti-aliased outer boundary mask (zero outside card)
    float shapeAlpha = saturate(0.5 - sd);
    if (shapeAlpha <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // 2. Hardware Bilinear Upsampling of 2D Gaussian Blurred Texture
    float2 backdropSize = u_cardSize + u_margin * 2.0;
    float2 sampleCenter = coord + u_margin;
    float2 centerUV = clamp(sampleCenter / backdropSize, 0.0005, 0.9995);

    float4 blurredBackdrop = InputTexture.Sample(LinearSampler, centerUV);

    // 3. Semi-Transparent Dark Charcoal Surface Tint
    float3 charcoalTint = u_tintColor.rgb;
    blurredBackdrop.rgb = lerp(blurredBackdrop.rgb, charcoalTint, u_tintOpacity);

    // 4. Gentle Broad Surface Highlight (Soft overhead sheen, no harsh specular reflections)
    float topSheen = saturate(1.0 - input.TexCoord.y * 1.5) * u_highlightStrength;
    blurredBackdrop.rgb += float3(topSheen, topSheen, topSheen);

    // 5. Subtle Low-Opacity Translucent Border
    float borderDist = -sd;
    if (borderDist < 1.5)
    {
        float borderFactor = saturate(1.0 - abs(borderDist - 0.75) / 0.75);
        float borderSheen = lerp(0.85, 1.25, saturate(1.0 - input.TexCoord.y + (input.TexCoord.x - 0.5) * 0.3));
        blurredBackdrop.rgb += float3(1.0, 1.0, 1.0) * (borderFactor * u_borderOpacity * borderSheen);
    }

    // 6. DirectComposition Premultiplied Alpha Output
    blurredBackdrop.rgb *= shapeAlpha;
    blurredBackdrop.a = shapeAlpha;

    return float4(blurredBackdrop.rgb, blurredBackdrop.a);
}
