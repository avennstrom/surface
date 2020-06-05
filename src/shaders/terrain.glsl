layout(binding = 0) uniform ConstantBuffer
{
	mat4 u_localToNDCMatrix;
	//
	vec3 u_eyePos;
	float u_fogStart;
	//
	vec3 u_lightDir;
	float u_fogEnd;
	//
	vec3 u_fogColor;
	float u_fogRangeInv;
};