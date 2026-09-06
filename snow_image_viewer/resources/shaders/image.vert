#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 uv;

layout(std140, binding = 0) uniform ViewerUniforms {
    mat4 mvp;
    vec4 checkerLight;
    vec4 checkerDark;
    vec4 outputParameters;
    vec4 sourceParameters;
    vec4 textureParameters;
} uniforms;

void main()
{
    uv = texCoord;
    gl_Position = uniforms.mvp * vec4(position, 0.0, 1.0);
}

