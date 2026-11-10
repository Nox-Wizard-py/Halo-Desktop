// plasmaRim.hlsl - Windows 11 Native Multi-Harmonic Screen-Edge Plasma Rim
// Ported from Apple Metal MSL / AIR 2.7 bytecode (extracted/Lumn.app/Contents/Resources/default.metallib)
// Faithful reproduction with partial-perimeter revolving wave, multi-color gradient & soft white core

cbuffer RimConstants : register(b0)
{
    float2 ScreenSize;      // Physical screen dimensions in pixels (e.g. 2560, 1440)
    float2 NotchDimensions; // Hardcoded to (0, 0) on Windows displays
    float4 HotColor;        // Color 1: Primary vibrant accent (e.g. cyan/gold/magenta)
    float4 DeepColor;       // Color 4: Deep foundation base (e.g. violet/indigo)
    float4 Color2;          // Color 2: Secondary contrasting accent
    float4 Color3;          // Color 3: Tertiary luminous highlight
    float  Time;            // Elapsed continuous animation time in seconds (frozen when paused)
    float  Phase;           // Continuous revolving phase argument [0.0, inf) (frozen when paused)
    float  Balance;         // Chromatic balance ratio [0.15, 0.85]
    float  Bloom;           // Transient bloom impulse [0.0, 1.0]
    float  CornerRadius;    // Default 0.0 for rectangular PC monitors, configurable up to 24.0
    float  ThicknessMul;    // Rim thickness multiplier (default 1.0)
    float  GlowMultiplier;  // Overall glow intensity multiplier (default 1.0)
    float  AudioLevel;      // Continuous dynamic normalized audio energy [0.0, 1.0]
    float  BeatPulse;       // Instantaneous beat transient impulse [0.0, 1.0]
    float  RevealProgress;  // Organic activation reveal progress [0.0, 1.0] (1.0 = normal plasma)
    float  RevealCorner;    // Origin corner: 0=Top-Left, 1=Top-Right, 2=Bottom-Right, 3=Bottom-Left
    float  RevealPadding;   // Alignment padding (total 128 bytes)
};

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

float4 PSMain(VS_OUTPUT input) : SV_Target
{
    float2 pixelPos = input.Position.xy;
    float2 halfSize = ScreenSize * 0.5f;
    float2 p = pixelPos - halfSize;

    // 1. Signed Distance to Screen Boundary (inward distance > 0)
    float effectiveRadius = max(0.0f, CornerRadius);
    float2 q = abs(p) - halfSize + effectiveRadius;
    float edgeDist = -(length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - effectiveRadius);

    // Camera notch adaptation (MacBook only; skipped when NotchDimensions.x <= 0)
    if (NotchDimensions.x > 0.0f)
    {
        float2 notchCenter = float2(0.0f, (NotchDimensions.y - ScreenSize.y) * 0.5f);
        float2 pNotch = p - notchCenter;
        float2 notchHalf = NotchDimensions * 0.5f;
        float2 qNotch = abs(pNotch) - notchHalf + 8.0f;
        float dNotch = length(max(qNotch, 0.0f)) + min(max(qNotch.x, qNotch.y), 0.0f) - 8.0f;
        edgeDist = min(edgeDist, max(dNotch, 0.0f));
    }

    // 2. Continuous Rectangle Perimeter Coordinate (uPerim in [0.0, 1.0) clockwise from Top-Left)
    float perim = max(2.0f * (ScreenSize.x + ScreenSize.y), 1.0f);
    float perimDist = 0.0f;
    float2 dEdge = float2(pixelPos.x, ScreenSize.x - pixelPos.x); // dist to Left, Right
    float2 dVert = float2(pixelPos.y, ScreenSize.y - pixelPos.y); // dist to Top, Bottom

    // Perimeter distance clockwise from Top-Left corner:
    if (dVert.x <= dEdge.x && dVert.x <= dEdge.y && dVert.x <= dVert.y)
    {
        perimDist = pixelPos.x; // Top edge: x in [0, W]
    }
    else if (dEdge.y <= dEdge.x && dEdge.y <= dVert.x && dEdge.y <= dVert.y)
    {
        perimDist = ScreenSize.x + pixelPos.y; // Right edge: y in [0, H]
    }
    else if (dVert.y <= dEdge.x && dVert.y <= dEdge.y && dVert.y <= dVert.x)
    {
        perimDist = ScreenSize.x + ScreenSize.y + (ScreenSize.x - pixelPos.x); // Bottom edge: x in [W, 0]
    }
    else
    {
        perimDist = 2.0f * ScreenSize.x + ScreenSize.y + (ScreenSize.y - pixelPos.y); // Left edge: y in [H, 0]
    }

    float uPerim = perimDist / perim;

    // 3. Screen-Space Corner Anchoring
    float uCorner = 0.0f;
    if (RevealCorner < 0.5f)
    {
        uCorner = 0.0f; // Corner 0: Top-Left (0, 0)
    }
    else if (RevealCorner < 1.5f)
    {
        uCorner = ScreenSize.x / perim; // Corner 1: Top-Right (W, 0)
    }
    else if (RevealCorner < 2.5f)
    {
        uCorner = 0.5f; // Corner 2: Bottom-Right (W, H)
    }
    else
    {
        uCorner = (2.0f * ScreenSize.x + ScreenSize.y) / perim; // Corner 3: Bottom-Left (0, H)
    }

    // 4. Plasma Segment Spatial Placement & Anchor
    // The plasma wave crest is anchored to the selected corner, advancing smoothly with Phase
    float waveCenter = frac(uCorner + Phase);
    float cyclicDiff = uPerim - waveCenter;
    cyclicDiff = cyclicDiff - round(cyclicDiff); // Wrapped to [-0.5, +0.5]
    float segDist = abs(cyclicDiff);

    // 5. Partial-Perimeter Segment Envelope (~50% active perimeter coverage)
    // Active half-span = 0.26 (covers ~52% of total monitor perimeter, centered at the corner)
    const float activeHalfSpan = 0.26f;
    float segmentEnv = smoothstep(activeHalfSpan, 0.04f, segDist);
    const float ambientFloor = 0.03f; // subtle baseline aura so inactive side is not completely dead
    float waveEnv = ambientFloor + (1.0f - ambientFloor) * segmentEnv;

    // 6. Multi-Harmonic Wave Along Perimeter (Recovered LUMN 3-harmonic equation)
    float t = Phase + uPerim;
    float wave = 0.50f * sin(t * 18.849556f - Time * 1.4f)
               + 0.35f * sin(t * 37.699112f + Time * 0.9f)
               + 0.25f * sin(t * 56.548668f + Time * 2.1f);

    // 7. Organic Plasma Activation Reveal Mask
    // Distance from pixel to origin corner along both adjoining edges
    float dU_corner = abs(frac(uPerim - uCorner + 0.5f) - 0.5f);
    float revealMask = 1.0f;
    float meniscusGlow = 0.0f;

    if (RevealProgress < 1.0f)
    {
        const float feather = 0.04f;
        float fluidRipple = (0.015f * sin(uPerim * 37.699112f - Time * 2.5f)
                           + 0.008f * cos(uPerim * 62.831853f + Time * 3.8f))
                          * saturate(1.0f - (dU_corner / activeHalfSpan));
        float frontEdge = 0.02f + RevealProgress * (activeHalfSpan + feather - 0.02f);
        float dFront = (frontEdge - dU_corner) + fluidRipple;
        revealMask = smoothstep(-feather, feather, dFront);

        // Leading edge liquid light meniscus glow
        float frontDist = abs(dFront);
        meniscusGlow = exp(-frontDist * frontDist / 0.0020f) * saturate(1.0f - RevealProgress * 1.15f);
    }

    // Coupled effective wave envelope during entrance animation
    float effectiveWaveEnv = (RevealProgress < 1.0f)
        ? (ambientFloor + (1.0f - ambientFloor) * segmentEnv * revealMask)
        : waveEnv;

    // 8. Dynamic Rim Reach / Thickness Calculation (Expanded atmospheric edge reach)
    float effectiveBloom = saturate(Bloom + BeatPulse * 0.40f);
    float baseReach = (14.0f + wave * 9.0f) * effectiveWaveEnv * max(0.1f, ThicknessMul);
    float reach = max(baseReach + effectiveBloom * 12.0f, 3.0f * effectiveWaveEnv);

    // Early discard: pixels deeper than reach + falloff margin
    if (edgeDist > reach + 3.5f)
    {
        discard;
    }

    // 9. Artwork Multi-Color Palette Blend Across Active Perimeter
    // Parameterize active illuminated span from trailing tail to leading head [0.0, 1.0]
    float s = saturate((cyclicDiff + activeHalfSpan) / (2.0f * activeHalfSpan));

    // Dynamic balance modulation of palette transition knot
    float splitMid = lerp(0.45f, 0.55f, Balance);

    float3 artworkCol;
    if (s < 0.35f)
    {
        float tSub = smoothstep(0.08f, 0.35f, s);
        artworkCol = lerp(DeepColor.rgb, Color2.rgb, tSub);
    }
    else if (s < splitMid + 0.15f)
    {
        float tSub = smoothstep(0.35f, splitMid + 0.15f, s);
        artworkCol = lerp(Color2.rgb, HotColor.rgb, tSub);
    }
    else
    {
        float tSub = smoothstep(splitMid + 0.15f, 0.92f, s);
        artworkCol = lerp(HotColor.rgb, Color3.rgb, tSub);
    }

    // Saturation and luma boost (Rec. 601 coefficients: 0.299, 0.587, 0.114)
    float luma = dot(artworkCol, float3(0.299f, 0.587f, 0.114f));
    float3 satColor = saturate(lerp(float3(luma, luma, luma), artworkCol, 1.35f) * 1.06f);
    float brightScale = segmentEnv * 0.45f + 1.0f;
    float3 col = saturate(satColor * brightScale);

    // 10. Soft White Luminous Highlight at Wave Crest (Centered at selected corner!)
    float angularCore = saturate(1.0f - segDist / 0.09f);
    float depthCore = saturate(1.0f - edgeDist / max(reach * 0.45f, 1.0f));
    float whiteIntensity = angularCore * depthCore * depthCore * 0.40f;
    col = lerp(col, float3(1.0f, 1.0f, 1.0f), whiteIntensity);

    // Add incandescent liquid meniscus tip during reveal
    if (RevealProgress < 1.0f)
    {
        col = saturate(col + meniscusGlow * 0.45f * lerp(HotColor.rgb, float3(1.0f, 1.0f, 1.0f), 0.75f));
    }

    // Dynamic bloom surge modulation
    if (effectiveBloom > 0.0f)
    {
        float bloomLuma = dot(col, float3(0.299f, 0.587f, 0.114f));
        float3 satBoost = lerp(float3(bloomLuma, bloomLuma, bloomLuma), col, 1.0f + effectiveBloom * 0.70f);
        col = saturate(satBoost * (1.0f + effectiveBloom * 0.85f));
    }

    // 11. Smooth Hermite Falloff (Proportional soft cubic transition)
    float falloffSpan = max(reach * 0.35f, 3.0f);
    float alphaFactor = saturate((reach - edgeDist) / falloffSpan);
    float smoothAlpha = alphaFactor * alphaFactor * (3.0f - 2.0f * alphaFactor);

    // Output Premultiplied Alpha for DirectComposition
    float finalAlpha = saturate(smoothAlpha * effectiveWaveEnv * GlowMultiplier * HotColor.a);
    if (RevealProgress < 1.0f)
    {
        finalAlpha *= revealMask;
    }

    return float4(col * finalAlpha, finalAlpha);
}
