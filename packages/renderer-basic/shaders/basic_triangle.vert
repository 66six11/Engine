#version 460

layout(location = 0) out vec3 outColor;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(0.0, -0.55),
        vec2(0.55, 0.45),
        vec2(-0.55, 0.45)
    );
    const vec3 colors[3] = vec3[3](
        vec3(0.95, 0.24, 0.18),
        vec3(0.12, 0.72, 0.36),
        vec3(0.18, 0.38, 0.95)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outColor = colors[gl_VertexIndex];
}
