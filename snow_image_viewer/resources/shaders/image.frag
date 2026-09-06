#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragmentColor;

layout(std140, binding = 0) uniform ViewerUniforms {
    mat4 mvp;
    vec4 checkerLight;
    vec4 checkerDark;
    vec4 outputParameters;
    vec4 sourceParameters;
    vec4 textureParameters;
} uniforms;

layout(binding = 1) uniform sampler2D imageTexture;

vec2 sourceCoordinates(vec2 coordinates)
{
    if (uniforms.textureParameters.z < -0.5)
        coordinates.y = 1.0 - coordinates.y;
    return coordinates;
}

float cubicWeight(float distance)
{
    const float absoluteDistance = abs(distance);
    if (absoluteDistance <= 1.0) {
        return 1.5 * absoluteDistance * absoluteDistance * absoluteDistance
               - 2.5 * absoluteDistance * absoluteDistance + 1.0;
    }
    if (absoluteDistance < 2.0) {
        return -0.5 * absoluteDistance * absoluteDistance * absoluteDistance
               + 2.5 * absoluteDistance * absoluteDistance
               - 4.0 * absoluteDistance + 2.0;
    }
    return 0.0;
}

vec4 sampleMagnifiedImage(vec2 coordinates)
{
    vec2 dimensions = uniforms.textureParameters.xy;
    vec2 textureCoordinates = coordinates * dimensions - vec2(0.5);
    ivec2 baseTexel = ivec2(floor(textureCoordinates));
    vec2 fraction = fract(textureCoordinates);

    vec4 result = vec4(0.0);
    for (int y = -1; y <= 2; ++y) {
        const float yWeight = cubicWeight(float(y) - fraction.y);
        for (int x = -1; x <= 2; ++x) {
            const float weight = cubicWeight(float(x) - fraction.x) * yWeight;
            const vec2 sampleCoordinates =
                (vec2(baseTexel) + vec2(float(x), float(y)) + vec2(0.5)) / dimensions;
            result += texture(imageTexture, sourceCoordinates(sampleCoordinates)) * weight;
        }
    }
    return result;
}

vec4 sampleImage(vec2 coordinates)
{
    vec2 sourcePixelsPerFragment = fwidth(coordinates) * uniforms.textureParameters.xy;
    if (max(sourcePixelsPerFragment.x, sourcePixelsPerFragment.y) <= 1.0) {
        return sampleMagnifiedImage(coordinates);
    }
    return texture(imageTexture, sourceCoordinates(coordinates));
}

vec3 acesFitted(vec3 value)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

vec3 compressToPeak(vec3 value, float peak)
{
    float luminance = max(dot(value, vec3(0.2126, 0.7152, 0.0722)), 0.000001);
    float knee = max(peak * 0.72, 1.0);
    if (luminance <= knee || peak <= knee) {
        return value;
    }
    float compressed = knee + (peak - knee) * (1.0 - exp(-(luminance - knee) / (peak - knee)));
    return value * (compressed / luminance);
}

void main()
{
    vec4 sampleValue = sampleImage(uv);
    float alpha = clamp(sampleValue.a, 0.0, 1.0);
    vec3 straight = uniforms.sourceParameters.w > 0.5
                        ? sampleValue.rgb
                        : (alpha > 0.00001 ? sampleValue.rgb / alpha : vec3(0.0));
    straight *= uniforms.sourceParameters.x;

    bool sourceHdr = uniforms.sourceParameters.y > 0.5;
    bool outputHdr = uniforms.outputParameters.x > 0.5;
    if (sourceHdr && outputHdr) {
        straight = compressToPeak(straight, uniforms.outputParameters.w);
    } else if (sourceHdr) {
        straight = acesFitted(straight / uniforms.sourceParameters.z);
    } else if (outputHdr) {
        straight *= uniforms.outputParameters.z;
    }

    vec2 checkerCell = floor(gl_FragCoord.xy / max(uniforms.outputParameters.y, 1.0));
    bool lightCell = mod(checkerCell.x + checkerCell.y, 2.0) < 1.0;
    vec3 checker = lightCell ? uniforms.checkerLight.rgb : uniforms.checkerDark.rgb;
    fragmentColor = vec4(straight * alpha + checker * (1.0 - alpha), 1.0);
}

