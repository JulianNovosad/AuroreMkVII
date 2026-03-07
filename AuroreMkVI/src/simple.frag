#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable // Explicitly enable Vulkan GLSL extensions

layout(set = 0, binding = 0) uniform sampler2D samplerColor;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(samplerColor, fragTexCoord);
}