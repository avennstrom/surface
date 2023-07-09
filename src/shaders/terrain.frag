#version 450
#extension GL_GOOGLE_include_directive : require

#include "utils.glsl"
#include "terrain.glsl"

layout(location = 0) in vec3 in_Color;
layout(location = 1) in float in_DistanceToEye;

layout(location = 0) out vec3 out_Color;

void main()
{
	float fogRangeInv = 1.0 / (u_fogEnd - u_fogStart);
	float fogAmount = sqrt(saturate((in_DistanceToEye - u_fogStart) * fogRangeInv));
	vec3 color = lerp(in_Color, u_fogColor, fogAmount);
	out_Color = color;
}
