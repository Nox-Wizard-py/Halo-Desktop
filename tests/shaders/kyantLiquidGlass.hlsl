// ==============================================================================
// Kyant0/AndroidLiquidGlass - HLSL Port for LUMN-REA
// Reference: https://github.com/Kyant0/AndroidLiquidGlass (Apache License 2.0)
// Copyright 2025 Kyant
// ==============================================================================

cbuffer GlassConstants : register(b0)
{
    float2 u_size;                // Card size in physical pixels (e.g. 380, 140)
    float2 u_offset;              // Coordinate offset / padding (0, 0)
    float4 u_cornerRadii;         // (topLeft, topRight, bottomRight, bottomLeft)
    float u_refractionHeight;     // Rim refraction thickness in pixels
    float u_refractionAmount;     // Refraction displacement magnitude (negative = inward bend)
    float u_depthEffect;          // Spherical depth contribution (0.0 to 1.0)
    float u_chromaticAberration;  // Dispersion intensity (0.0 to 1.0)
    float u_highlightAngle;       // Specular light angle in radians
    float u_highlightFalloff;     // Specular sharpness exponent
    float u_vibrancy;             // Backdrop saturation multiplier (1.0 = neutral, 1.5 = vibrant)
    float u_blurRadius;           // Reserved blur parameter
    float4 u_highlightColor;      // Specular rim light color and intensity
    float4 u_tintColor;           // Glass body tint and opacity
};

Texture2D g_BackdropTexture : register(t0);
SamplerState g_LinearSampler : register(s0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

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

float4 sampleBackdrop(float2 pixelCoord)
{
    float2 uv = pixelCoord / u_size;
    uv = clamp(uv, 0.0005, 0.9995);
    float4 s = g_BackdropTexture.Sample(g_LinearSampler, uv);
    s.rgb = applyVibrancy(s.rgb, u_vibrancy);
    return s;
}

// ------------------------------------------------------------------------------
// Main Fragment Shader
// ------------------------------------------------------------------------------

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    float2 coord = input.uv * u_size;
    float2 halfSize = u_size * 0.5;
    float2 centeredCoord = (coord + u_offset) - halfSize;
    float radius = radiusAt(centeredCoord, u_cornerRadii);
    
    // Evaluate Signed Distance Field
    float sd = sdRoundedRect(centeredCoord, halfSize, radius);
    
    // Anti-aliased outer boundary mask
    float shapeAlpha = saturate(0.5 - sd);
    if (shapeAlpha <= 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    
    // Kyant0 Center Check:
    // If inside the shape by more than refractionHeight, return unrefracted backdrop!
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
    
    // Kyant0 7-band Spectral Dispersion
    if (inRefractionZone && u_chromaticAberration > 0.001 && length(dispersedCoord) > 0.02)
    {
        // Red (+1.0)
        float4 red = sampleBackdrop(refractedCoord + dispersedCoord);
        color.r += red.r / 3.5;
        color.a += red.a / 7.0;
        
        // Orange (+0.667)
        float4 orange = sampleBackdrop(refractedCoord + dispersedCoord * (2.0 / 3.0));
        color.r += orange.r / 3.5;
        color.g += orange.g / 7.0;
        color.a += orange.a / 7.0;
        
        // Yellow (+0.333)
        float4 yellow = sampleBackdrop(refractedCoord + dispersedCoord * (1.0 / 3.0));
        color.r += yellow.r / 3.5;
        color.g += yellow.g / 3.5;
        color.a += yellow.a / 7.0;
        
        // Green (Central wavelength)
        float4 green = sampleBackdrop(refractedCoord);
        color.g += green.g / 3.5;
        color.a += green.a / 7.0;
        
        // Cyan (-0.333)
        float4 cyan = sampleBackdrop(refractedCoord - dispersedCoord * (1.0 / 3.0));
        color.g += cyan.g / 3.5;
        color.b += cyan.b / 3.0;
        color.a += cyan.a / 7.0;
        
        // Blue (-0.667)
        float4 blue = sampleBackdrop(refractedCoord - dispersedCoord * (2.0 / 3.0));
        color.b += blue.b / 3.0;
        color.a += blue.a / 7.0;
        
        // Purple (-1.0)
        float4 purple = sampleBackdrop(refractedCoord - dispersedCoord);
        color.r += purple.r / 7.0;
        color.b += purple.b / 3.0;
        color.a += purple.a / 7.0;
    }
    else
    {
        color = sampleBackdrop(refractedCoord);
    }
    
    // Glass Body Tint
    color.rgb = lerp(color.rgb, u_tintColor.rgb, u_tintColor.a);
    
    // Kyant0 Directional Specular Highlight
    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 highlightGrad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
    float2 lightNormal = float2(cos(u_highlightAngle), sin(u_highlightAngle));
    float dHighlight = dot(highlightGrad, lightNormal);
    float highlightIntensity = pow(abs(dHighlight), u_highlightFalloff);
    
    // Concentrated at outer perimeter
    float edgeProximity = saturate(1.0 - (-min(sd, 0.0) / max(1.0, u_refractionHeight * 0.4)));
    float4 highlight = u_highlightColor * (highlightIntensity * edgeProximity);
    color.rgb += highlight.rgb * highlight.a;
    
    color.a *= shapeAlpha;
    return color;
}

