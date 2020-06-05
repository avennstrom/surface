float saturate(float val) 
{
	return clamp(val, 0.0, 1.0);
}

float lerp(float a, float b, float t)
{
	return mix(a, b, t);
}

vec2 lerp(vec2 a, vec2 b, float t)
{
	return mix(a, b, t);
}

vec3 lerp(vec3 a, vec3 b, float t)
{
	return mix(a, b, t);
}

vec4 lerp(vec4 a, vec4 b, float t)
{
	return mix(a, b, t);
}
