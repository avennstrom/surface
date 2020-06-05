#pragma once

#include <cstdint>

class Terrain
{
public:
	static void init(int seed);
	static double surface(double x, double y, double z);

	static void sample(float* values, int32_t x, int32_t y, int32_t z, int32_t w, int32_t h, int32_t d, float scale);
};