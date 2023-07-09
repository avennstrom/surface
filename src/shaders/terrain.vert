#version 450
#extension GL_GOOGLE_include_directive : require
//#extension GL_EXT_nonuniform_qualifier : require

#include "utils.glsl"
#include "terrain.glsl"

layout(location = 0) in vec3 in_Position;
layout(location = 1) in vec3 in_Normal;

layout(location = 0) out vec3 out_Color;
layout(location = 1) out float out_DistanceToEye;

void main()
{
	float intensity = pow(saturate(dot(in_Normal.xyz, -normalize(u_lightDir)) * 0.5 + 0.5), 2.0);
	float intensity2 = 0.0;

	/*vec3 color = mix(
		vec3(64.0, 41.0, 5.0) / vec3(255.0, 255.0, 255.0), 
		vec3(0.0, 1.0, 0.0), 
		saturate(-in_Normal.y - 0.5));*/

	float sunBrightness = 100.0;
	intensity *= sunBrightness;

	vec3 color = vec3(0.3, 0.3, 0.35);

	gl_Position = u_localToNDCMatrix * vec4(in_Position, 1);
	out_Color = color * intensity.xxx + color * intensity2.xxx;
	out_DistanceToEye = length(in_Position - u_eyePos.xyz);
}