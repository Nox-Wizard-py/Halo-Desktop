// plasmaRim.hlsl - Windows 11 Native Rim Lighting Pixel Shader
// Reconstructed from Lumn 1.1 Metal AIR Bitcode (scratch/plasmaRim.ll)

cbuffer RimConstants : register(b0)
{
    float2 ScreenSize;      // Physical screen dimensions in pixels (e.g. 2560, 1440)
    float2 NotchDimensions; // Hardcoded to (0, 0) on Windows displays
    float4 HotColor;        // High-energy rim color (RGBA float, e.g. cyan/magenta)
    float4 DeepColor;       // Low-energy bass rim color (RGBA float, e.g. indigo/purple)
    float  Time;            // Elapsed continuous animation time in seconds
    float  Phase;           // Beat-synchronized phase [0.0, 1.0] (defaults to idle progress)
    float  Balance;         // Chromatic balance ratio [0.0, 1.0]
    float  Bloom;           // Transient bloom impulse [0.0, 1.0]
    float  CornerRadius;    // Default 0.0 for rectangular PC monitors, configurable up to 24.0
    float  ThicknessMul;    // Rim thickness multiplier (default 1.0)
    float  GlowMultiplier;  // Overall glow intensity multiplier (default 1.0)
    float  AudioLevel;      // Continuous dynamic normalized audio energy [0.0, 1.0]
    float  BeatPulse;       // Instantaneous beat transient impulse [0.0, 1.0]
    float3 Padding;         // 16-byte alignment padding (total 96 bytes)
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

// 2D Hash Function (matching procedural noise in Metal shader)
float hash2d(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// 2D Value Noise with Hermite Interpolation
float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);

    float a = hash2d(i + float2(0.0f, 0.0f));
    float b = hash2d(i + float2(1.0f, 0.0f));
    float c = hash2d(i + float2(0.0f, 1.0f));
    float d = hash2d(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 4-Octave Fractional Brownian Motion (FBM)
float fbm(float2 p)
{
    float v = 0.0f;
    float a = 0.5f;
    float2 shift = float2(100.0f, 100.0f);
    float2x2 rot = float2x2(cos(0.5f), sin(0.5f), -sin(0.5f), cos(0.5f));
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        v += a * valueNoise(p);
        p = mul(rot, p) * 2.0f + shift;
        a *= 0.5f;
    }
    return v;
}

// Signed Distance Field (SDF) for a 2D rounded rectangle
float sdRoundedBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float4 PSMain(VS_OUTPUT input) : SV_Target
{
    float2 pixelPos = input.Position.xy;
    float2 halfSize = ScreenSize * 0.5f;
    float2 p = pixelPos - halfSize;

    // 1. Signed Distance to Screen Boundary
    // Windows displays do not have a camera notch: NotchDimensions is (0, 0)
    float effectiveRadius = max(0.0f, CornerRadius);
    float dist = sdRoundedBox(p, halfSize, effectiveRadius);
    float edgeDist = abs(dist);

    // 2. Base Rim Geometry Bounds & Dynamic Breathing
    float baseThickness = 4.0f * max(0.2f, ThicknessMul);

    // Continuous organic audio breathing: expands and contracts glow with audio energy
    float continuousBreathe = AudioLevel * (2.2f + 1.2f * sin(Time * 2.8f + Phase * 6.2831853f));

    // Discrete beat impact: sharp kick expansion that snaps with the rhythm
    float beatImpact = BeatPulse * 3.5f + Bloom * 8.0f;

    // Maximum reach for early fragment discard
    float maxReach = baseThickness * (1.0f + AudioLevel * 0.4f) + continuousBreathe + beatImpact + 14.0f;
    if (edgeDist > maxReach)
    {
        discard;
    }

    // 3. Perimeter Coordinate Projection t in [0, Perimeter]
    // Unwraps rectangular perimeter into a continuous 1D domain
    float perimeter = (ScreenSize.x + ScreenSize.y) * 2.0f;
    float dTop = pixelPos.y;
    float dBottom = ScreenSize.y - pixelPos.y;
    float dLeft = pixelPos.x;
    float dRight = ScreenSize.x - pixelPos.x;
    float minD = min(min(dTop, dBottom), min(dLeft, dRight));

    float t = 0.0f;
    if (minD == dTop)
    {
        t = pixelPos.x; // Top edge (left to right)
    }
    else if (minD == dRight)
    {
        t = ScreenSize.x + pixelPos.y; // Right edge (top to bottom)
    }
    else if (minD == dBottom)
    {
        t = ScreenSize.x + ScreenSize.y + (ScreenSize.x - pixelPos.x); // Bottom edge (right to left)
    }
    else
    {
        t = ScreenSize.x * 2.0f + ScreenSize.y + (ScreenSize.y - pixelPos.y); // Left edge (bottom to top)
    }
    float normT = t / max(1.0f, perimeter);

    // 4. Multi-Harmonic Wave Animation & Continuous Energy Ripples
    // Forward-additive propagation avoids destructive velocity cancellation between Time and Phase
    float twoPi = 6.2831853f;

    // Fundamental harmonic: perimeter wave locked to Phase with smooth continuous Time drift
    float phi1 = normT * twoPi * 2.0f + (Phase * twoPi) + (Time * 0.40f);
    float h1 = 0.50f * sin(phi1);

    // Secondary harmonic: ripple component modulated continuously by live audio energy
    float phi2 = normT * twoPi * 5.0f - (Time * 0.75f) - (Phase * 3.14159f);
    float h2 = 0.35f * sin(phi2) * (0.45f + AudioLevel * 0.95f);

    // Tertiary flutter: fine turbulence energized during louder passages
    float phi3 = normT * twoPi * 8.0f + (Time * 1.50f) + (Phase * twoPi * 1.5f);
    float h3 = 0.20f * sin(phi3) * (0.20f + AudioLevel * 1.40f);

    float waveTotal = h1 + h2 + h3;
    float plasma = saturate(waveTotal * 0.5f + 0.5f);

    // Modulate with 4-octave spatial procedural noise
    // Noise rate accelerates with live music energy for dynamic surface flow
    float noiseRate = 0.18f + AudioLevel * 0.36f;
    float noiseVal = fbm(float2(normT * 8.0f + Phase * 0.4f, Time * noiseRate));
    plasma = saturate(plasma * 0.68f + noiseVal * 0.32f);

    // 5. Chromatic Mixing (Deep to Hot Colors)
    // Audio energy and beat impacts dynamically shift the balance towards HotColor
    float mixRatio = saturate(plasma * Balance + BeatPulse * 0.35f + Bloom * 0.45f + AudioLevel * 0.30f);
    float4 activeColor = lerp(DeepColor, HotColor, mixRatio);

    // 6. Transverse Exponential Intensity Falloff & Bloom
    // Core rim line expands with continuous energy and transient beats
    float dynamicThickness = baseThickness * (1.0f + AudioLevel * 0.35f + BeatPulse * 0.30f);
    float coreFalloff = exp(-edgeDist / (dynamicThickness * 0.85f));

    float ambientSpan = dynamicThickness * (1.8f + AudioLevel * 0.70f + BeatPulse * 1.10f + Bloom * 2.20f);
    float ambientFalloff = exp(-edgeDist / max(1.0f, ambientSpan)) * 0.65f;
    float totalIntensity = saturate(coreFalloff + ambientFalloff);

    // Dynamic glow boost: continuous audio radiance with explosive beat flashes
    float dynamicBoost = 1.0f + AudioLevel * 0.60f + BeatPulse * 0.45f + Bloom * 0.85f;
    float alpha = saturate(totalIntensity * activeColor.a * GlowMultiplier * dynamicBoost);

    // 7. Output Premultiplied Alpha for DirectComposition
    return float4(activeColor.rgb * alpha, alpha);
}
