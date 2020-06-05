#version 450
#extension GL_GOOGLE_include_directive : require

#include "debug.glsl"

layout(location = 0) in vec3 in_Position;
layout(location = 1) in vec3 in_Color;

layout(location = 0) out vec3 out_Color;

void main()
{
	gl_Position = u_localToNDCMatrix * vec4(in_Position, 1);
	out_Color = in_Color;
}