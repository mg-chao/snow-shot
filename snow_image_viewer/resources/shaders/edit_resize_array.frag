#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragmentColor;

layout(std140, binding = 0) uniform ResizeUniforms {
    mat4 mvp;
    vec4 sourceAndTarget;
    vec4 options;
    vec4 scaleAndAxis;
    ivec4 targetTile;
    vec4 backendParameters;
} uniforms;

layout(binding = 1) uniform sampler2DArray sourceTexture;

float sinc(float value)
{
    if (abs(value) < 0.00001) return 1.0;
    const float scaled = 3.14159265359 * value;
    return sin(scaled) / scaled;
}

float lanczos3(float value)
{
    value = abs(value);
    return value < 3.0 ? sinc(value) * sinc(value / 3.0) : 0.0;
}

vec3 linearToSrgb(vec3 value)
{
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 highValue = 1.055 * pow(max(value, vec3(0.0)),
                                 vec3(1.0 / 2.4)) - 0.055;
    return mix(highValue, value * 12.92, low);
}

vec3 srgbToLinear(vec3 value)
{
    bvec3 low = lessThanEqual(value, vec3(0.04045));
    vec3 highValue = pow((max(value, vec3(0.0)) + 0.055) / 1.055,
                         vec3(2.4));
    return mix(highValue, value / 12.92, low);
}

vec4 prepareSample(vec4 value)
{
    if (uniforms.backendParameters.w > 0.5) {
        if (uniforms.options.y > 0.5) value.rgb = srgbToLinear(value.rgb);
    } else if (uniforms.options.y < 0.5) {
        value.rgb = linearToSrgb(value.rgb);
    }
    if (uniforms.options.z > 0.5) value.rgb *= value.a;
    return value;
}

vec4 fetchVirtual(ivec2 texel, int level)
{
    ivec2 dimensions = max(
        ivec2(uniforms.sourceAndTarget.xy + vec2(0.5)), ivec2(1));
    ivec2 baseLayerDimensions =
        ivec2(uniforms.scaleAndAxis.zw + vec2(0.5));
    ivec2 layerDimensions = max(baseLayerDimensions >> level, ivec2(1));
    texel = clamp(texel, ivec2(0), dimensions - ivec2(1));
    if (uniforms.backendParameters.y > 0.5)
        texel.y = dimensions.y - 1 - texel.y;
    ivec2 tile = texel / layerDimensions;
    int columns = max(int(uniforms.options.w + 0.5), 1);
    int layer = tile.y * columns + tile.x;
    return prepareSample(texelFetch(
        sourceTexture, ivec3(texel - tile * layerDimensions, layer), level));
}

vec4 sampleNearestPixel(ivec2 targetPixel, ivec2 targetDimensions)
{
    ivec2 dimensions = max(
        ivec2(uniforms.sourceAndTarget.xy + vec2(0.5)), ivec2(1));
    uvec2 numerator = (uvec2(targetPixel) * 2u + 1u) *
                      uvec2(dimensions);
    uvec2 denominator = 2u * uvec2(max(targetDimensions, ivec2(1)));
    ivec2 texel = ivec2(numerator / denominator);
    return fetchVirtual(texel, 0);
}

float sourcePositionComponent(int targetPixel, int targetExtent,
                              int sourceExtent, out int base)
{
    int denominator = 2 * max(targetExtent, 1);
    int numerator = (2 * targetPixel + 1) * sourceExtent - targetExtent;
    base = numerator >= 0
        ? numerator / denominator
        : -((-numerator + denominator - 1) / denominator);
    return float(numerator - base * denominator) / float(denominator);
}

vec2 sourcePosition(ivec2 targetPixel, ivec2 targetDimensions,
                    ivec2 sourceDimensions, out ivec2 base)
{
    vec2 fraction;
    fraction.x = sourcePositionComponent(
        targetPixel.x, targetDimensions.x, sourceDimensions.x, base.x);
    fraction.y = sourcePositionComponent(
        targetPixel.y, targetDimensions.y, sourceDimensions.y, base.y);
    return fraction;
}

vec4 sampleLanczos2D(ivec2 targetPixel, ivec2 targetDimensions,
                     float mipLevel)
{
    ivec2 dimensions = max(
        ivec2(uniforms.sourceAndTarget.xy + vec2(0.5)), ivec2(1));
    ivec2 base;
    vec2 fraction = sourcePosition(
        targetPixel, targetDimensions, dimensions, base);
    vec4 total = vec4(0.0);
    float totalWeight = 0.0;
    for (int y = -2; y <= 3; ++y) {
        float yWeight = lanczos3(float(y) - fraction.y);
        for (int x = -2; x <= 3; ++x) {
            float weight = lanczos3(float(x) - fraction.x) * yWeight;
            total += fetchVirtual(base + ivec2(x, y),
                                  int(mipLevel + 0.5)) * weight;
            totalWeight += weight;
        }
    }
    return total / max(totalWeight, 0.00001);
}

vec4 sampleLanczosAxis(ivec2 targetPixel, ivec2 targetDimensions,
                       float mipLevel, int axis)
{
    ivec2 dimensions = max(
        ivec2(uniforms.sourceAndTarget.xy + vec2(0.5)), ivec2(1));
    ivec2 base;
    vec2 fraction = sourcePosition(
        targetPixel, targetDimensions, dimensions, base);
    if (axis == 1) {
        base.y += fraction.y >= 0.5 ? 1 : 0;
        fraction.y = 0.0;
    } else {
        base.x += fraction.x >= 0.5 ? 1 : 0;
        fraction.x = 0.0;
    }
    vec4 total = vec4(0.0);
    float totalWeight = 0.0;
    for (int tap = -2; tap <= 3; ++tap) {
        float distance = axis == 1 ? float(tap) - fraction.x
                                   : float(tap) - fraction.y;
        float weight = lanczos3(distance);
        ivec2 offset = axis == 1 ? ivec2(tap, 0) : ivec2(0, tap);
        total += fetchVirtual(base + offset, int(mipLevel + 0.5)) * weight;
        totalWeight += weight;
    }
    return total / max(totalWeight, 0.00001);
}

vec4 sampleLinear(ivec2 targetPixel, ivec2 targetDimensions)
{
    ivec2 dimensions = max(
        ivec2(uniforms.sourceAndTarget.xy + vec2(0.5)), ivec2(1));
    ivec2 base;
    vec2 fraction = sourcePosition(
        targetPixel, targetDimensions, dimensions, base);
    vec4 top = mix(fetchVirtual(base, 0),
                   fetchVirtual(base + ivec2(1, 0), 0), fraction.x);
    vec4 bottom = mix(fetchVirtual(base + ivec2(0, 1), 0),
                      fetchVirtual(base + ivec2(1, 1), 0), fraction.x);
    return mix(top, bottom, fraction.y);
}

void main()
{
    ivec2 tileDimensions = max(
        ivec2(uniforms.sourceAndTarget.zw + vec2(0.5)), ivec2(1));
    ivec2 targetDimensions = max(uniforms.targetTile.zw, ivec2(1));
    ivec2 localPixel = uniforms.backendParameters.z > 0.5
        ? ivec2(gl_FragCoord.xy)
        : ivec2(floor(clamp(uv, vec2(0.0), vec2(0.999999)) *
                      vec2(tileDimensions)));
    if (uniforms.backendParameters.x > 0.5)
        localPixel.y = tileDimensions.y - 1 - localPixel.y;
    ivec2 targetPixel = uniforms.targetTile.xy + localPixel;
    vec4 value;
    int axis = int(uniforms.scaleAndAxis.y + 0.5);
    float mipLevel = max(uniforms.scaleAndAxis.x, 0.0);
    if (uniforms.options.x > 1.5 && axis != 0)
        value = sampleLanczosAxis(targetPixel, targetDimensions,
                                  mipLevel, axis);
    else if (uniforms.options.x > 1.5)
        value = sampleLanczos2D(targetPixel, targetDimensions, mipLevel);
    else if (uniforms.options.x > 0.5)
        value = sampleLinear(targetPixel, targetDimensions);
    else
        value = sampleNearestPixel(targetPixel, targetDimensions);
    if (uniforms.options.y < 0.5 && uniforms.backendParameters.w < 0.5)
        value.rgb = srgbToLinear(value.rgb);
    float alpha = clamp(value.a, 0.0, 1.0);
    vec3 straight = uniforms.options.z > 0.5
                        ? (alpha > 0.00001 ? value.rgb / alpha : vec3(0.0))
                        : value.rgb;
    if (uniforms.backendParameters.w > 0.5 && uniforms.options.y > 0.5)
        straight = linearToSrgb(straight);
    fragmentColor = vec4(straight, alpha);
}
