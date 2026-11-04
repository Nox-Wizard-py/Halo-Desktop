// ==============================================================================
// HLSL Pixel Shader: liquidGlass.hlsl
// Production Liquid Glass Material with Real-Time Localized Backdrop Refraction
// Mathematical Reference: Kyant0/AndroidLiquidGlass (Apache License 2.0)
// Copyright 2025 Kyant
// Adapted for LUMN-REA Native Windows Media Card
// ==============================================================================

cbuffer LiquidGlassBuffer : register(b0)
{
    float2 u_cardSize;            // Physical card dimensions (width, height)
    float2 u_margin;              // Backdrop sampling margin (e.g. 24.0, 24.0)
    float4 u_cornerRadii;         // (topLeft, topRight, bottomRight, bottomLeft)
    float  u_refractionHeight;    // Refraction rim thickness in pixels (~22.0 px)
    float  u_refractionAmount;    // Lens displacement magnitude (~ -22.0 px, inward bend)
    float  u_depthEffect;         // Spherical depth contribution (0.0 to 1.0)
    float  u_chromaticAberration; // Corner chromatic dispersion intensity (~0.40)
    float  u_highlightAngle;      // Specular light direction angle in radians (45 deg)
    float  u_highlightFalloff;    // Specular sharpness exponent (~1.50)
    float  u_vibrancy;            // Backdrop saturation boost (~2.50x)
    float  u_specularStrength;    // Specular highlight opacity (~0.50)
    float4 u_highlightColor;      // Directional rim light color
    float4 u_tintColor;           // Base glass body tint (e.g. 0.06, 0.06, 0.10, 0.10)
    float4 u_hotColor;            // Dynamic hot accent color from OKLab artwork palette
    float4 u_deepColor;           // Dynamic deep color from OKLab artwork palette
    float  u_frostAmount;         // Frosted blur diffusion radius in pixels
    float  u_frostOpacity;        // Soft frosted translucent substrate scatter
    float2 u_padding;             // Alignment padding
};

Texture2D    BackdropTexture : register(t0);
SamplerState BackdropSampler : register(s0);

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
// Kyant0 Geometry & Signed Distance Fields
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

float2 gradSdRoundedRect(float2 coord, float2 halfSize, float radius)
{
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius, radius));
    if (cornerCoord.x >= 0.0 || cornerCoord.y >= 0.0) {
        return sign(coord) * normalize(max(cornerCoord, 0.0));
    } else {
        float gradX = step(cornerCoord.y, cornerCoord.x);
        return sign(coord) * float2(gradX, 1.0 - gradX);
    }
}

// ------------------------------------------------------------------------------
// Lens Displacement Mathematics
// ------------------------------------------------------------------------------

float circleMap(float x)
{
    return 1.0 - sqrt(max(0.0, 1.0 - x * x));
}

// ------------------------------------------------------------------------------
// Color Controls & Vibrancy
// ------------------------------------------------------------------------------

float3 applyVibrancy(float3 color, float saturation)
{
    if (abs(saturation - 1.0) < 0.001) return color;
    float luma = dot(color, float3(0.213, 0.715, 0.072));
    return lerp(float3(luma, luma, luma), color, saturation);
}

// Samples backdrop using physical card coordinate, mapping through the capture margin
float4 sampleBackdropSingle(float2 cardCoord)
{
    float2 backdropSize = u_cardSize + u_margin * 2.0;
    float2 samplePos = cardCoord + u_margin;
    float2 uv = samplePos / backdropSize;
    uv = clamp(uv, 0.0005, 0.9995);
    float4 s = BackdropTexture.Sample(BackdropSampler, uv);
    s.rgb = applyVibrancy(s.rgb, u_vibrancy);
    return s;
}

// Multi-tap disc diffusion sampler for a soft, silky frosted glass finish
float4 sampleBackdropFrosted(float2 cardCoord, float radius)
{
    if (radius <= 0.5)
    {
        return sampleBackdropSingle(cardCoord);
    }

    const float2 offsets[8] = {
        float2( 0.000,  0.350),
        float2(-0.476,  0.155),
        float2(-0.476, -0.443),
        float2( 0.000, -0.750),
        float2( 0.693, -0.490),
        float2( 0.875,  0.284),
        float2( 0.323,  0.915),
        float2(-0.551,  0.834)
    };

    float4 sum = sampleBackdropSingle(cardCoord) * 0.24;
    for (int i = 0; i < 8; ++i)
    {
        sum += sampleBackdropSingle(cardCoord + offsets[i] * radius) * 0.095;
    }
    return sum;
}

// ------------------------------------------------------------------------------
// Main Fragment Shader
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

    // 2. Kyant0 Center Check:
    // If inside the shape by more than refractionHeight, return unrefracted backdrop
    float2 refractedCoord = coord;
    float2 dispersedCoord = float2(0.0, 0.0);
    bool inRefractionZone = (-sd < u_refractionHeight);

    if (inRefractionZone)
    {
        float clampedSd = min(sd, 0.0);
        float d = circleMap(1.0 - -clampedSd / u_refractionHeight) * u_refractionAmount;
        float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));

        float2 baseGrad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
        float2 depthVec = u_depthEffect * (length(centeredCoord) > 0.001 ? normalize(centeredCoord) : float2(0.0, 0.0));
        float2 grad = normalize(baseGrad + depthVec);

        refractedCoord = coord + d * grad;

        // Kyant0 Chromatic Aberration formula:
        // Intensified in the 4 corners: ((x*y) / (w*h))
        float dispersionIntensity = u_chromaticAberration * ((centeredCoord.x * centeredCoord.y) / (halfSize.x * halfSize.y));
        dispersedCoord = d * grad * dispersionIntensity;
    }

    float4 color = float4(0.0, 0.0, 0.0, 0.0);

    // 3. Kyant0 7-band Spectral Dispersion (active in corners)
    if (inRefractionZone && u_chromaticAberration > 0.001 && length(dispersedCoord) > 0.02)
    {
        // Red (+1.0)
        float4 red = sampleBackdropSingle(refractedCoord + dispersedCoord);
        color.r += red.r / 3.5;
        color.a += red.a / 7.0;

        // Orange (+0.667)
        float4 orange = sampleBackdropSingle(refractedCoord + dispersedCoord * (2.0 / 3.0));
        color.r += orange.r / 3.5;
        color.g += orange.g / 7.0;
        color.a += orange.a / 7.0;

        // Yellow (+0.333)
        float4 yellow = sampleBackdropSingle(refractedCoord + dispersedCoord * (1.0 / 3.0));
        color.r += yellow.r / 3.5;
        color.g += yellow.g / 3.5;
        color.a += yellow.a / 7.0;

        // Green (Central wavelength)
        float4 green = sampleBackdropSingle(refractedCoord);
        color.g += green.g / 3.5;
        color.a += green.a / 7.0;

        // Cyan (-0.333)
        float4 cyan = sampleBackdropSingle(refractedCoord - dispersedCoord * (1.0 / 3.0));
        color.g += cyan.g / 3.5;
        color.b += cyan.b / 3.0;
        color.a += cyan.a / 7.0;

        // Blue (-0.667)
        float4 blue = sampleBackdropSingle(refractedCoord - dispersedCoord * (2.0 / 3.0));
        color.b += blue.b / 3.0;
        color.a += blue.a / 7.0;

        // Purple (-1.0)
        float4 purple = sampleBackdropSingle(refractedCoord - dispersedCoord);
        color.r += purple.r / 7.0;
        color.b += purple.b / 3.0;
        color.a += purple.a / 7.0;
    }
    else
    {
        color = sampleBackdropFrosted(refractedCoord, u_frostAmount);
    }

    // 4. Dynamic Artwork Palette Tint & Frosted Substrate Scatter
    float3 artworkTint = lerp(u_deepColor.rgb, u_hotColor.rgb, 0.35);
    float3 effectiveTintRgb = lerp(u_tintColor.rgb, artworkTint, 0.35);
    color.rgb = lerp(color.rgb, effectiveTintRgb, u_tintColor.a);

    // Soft frosted translucent ambient scatter lift
    float3 frostedScatter = lerp(float3(0.92, 0.94, 0.98), effectiveTintRgb, 0.40);
    color.rgb = lerp(color.rgb, frostedScatter, u_frostOpacity);

    // 5. Kyant0 Directional Specular Highlight Sheen
    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 highlightGrad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
    float2 lightNormal = float2(cos(u_highlightAngle), sin(u_highlightAngle));
    float dHighlight = dot(highlightGrad, lightNormal);
    float highlightIntensity = pow(abs(dHighlight), u_highlightFalloff);

    // Concentrated at outer perimeter
    float edgeProximity = saturate(1.0 - (-min(sd, 0.0) / max(1.0, u_refractionHeight * 0.45)));
    float4 highlight = u_highlightColor * (highlightIntensity * edgeProximity * u_specularStrength);
    color.rgb += highlight.rgb * highlight.a;

    // Strict Rounded-Rectangle Mask: Premultiplied alpha for DirectComposition
    color.rgb *= shapeAlpha;
    color.a = shapeAlpha;
    return float4(color.rgb, color.a);
}
