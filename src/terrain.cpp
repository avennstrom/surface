#include "terrain.hpp"

#include <noise/module/perlin.h>

#include <FastNoiseSIMD/FastNoiseSIMD.h>

#include <intrin.h>

#include <algorithm>
#include <memory>

//static noise::module::Perlin A;
static noise::module::Perlin B;

static std::unique_ptr<FastNoiseSIMD> fastNoise;
static std::unique_ptr<FastNoiseSIMD> fastNoise2;
static std::unique_ptr<FastNoiseSIMD> fastNoise3;
static std::unique_ptr<FastNoiseSIMD> fastNoise4;
static std::unique_ptr<FastNoiseSIMD> fastNoise5;

void Terrain::init(int seed)
{
	fastNoise.reset(FastNoiseSIMD::NewFastNoiseSIMD(seed));
	fastNoise->SetFrequency(0.00776f);
	fastNoise->SetFractalOctaves(5);
	//fastNoise->SetAxisScales(1.0f, 2.0f, 1.0f);

	fastNoise2.reset(FastNoiseSIMD::NewFastNoiseSIMD(seed));
	fastNoise2->SetFrequency(0.0036f);
	fastNoise2->SetFractalOctaves(4);

	fastNoise3.reset(FastNoiseSIMD::NewFastNoiseSIMD(seed));
	fastNoise3->SetFrequency(0.0025f);
	fastNoise3->SetFractalOctaves(10);
	//fastNoise3->SetAxisScales(1.0f, 7.0f, 1.0f);
	
	fastNoise4.reset(FastNoiseSIMD::NewFastNoiseSIMD(seed));
	fastNoise4->SetFrequency(0.0046f);
	fastNoise4->SetFractalOctaves(2);
	//fastNoise4->SetAxisScales(1.0f, 0.01f, 1.0f);

	fastNoise5.reset(FastNoiseSIMD::NewFastNoiseSIMD(seed));
	fastNoise5->SetFrequency(0.0006f);
	fastNoise5->SetFractalOctaves(8);
	//fastNoise4->SetAxisScales(1.0f, 0.01f, 1.0f);

	//A.SetSeed(seed);
	//A.SetFrequency(0.0032);
	//A.SetLacunarity(10.0);
	//A.SetOctaveCount(4);

	B.SetSeed(seed);
	B.SetFrequency(0.0176);
	B.SetOctaveCount(6);
}

//static double getValueRange(
//	noise::module::Module& m,
//	double x,
//	double y,
//	double z,
//	double min,
//	double max)
//{
//	return (m.GetValue(x, y, z) * 0.5 + 0.5) * (max - min) + min;
//}

double Terrain::surface(double x, double y, double z)
{
	//double groundFactor = std::min(1.0, std::max(0.0, (y - getValueRange(A, x, 0.0, z, 64.0, 256.0)) / 256.0));
	//double caveFactor = 1.0 - groundFactor;
	//
	//double groundNoise = -1;
	//double caveNoise = B.GetValue(x, y, z);

	//return caveFactor*caveNoise + groundFactor*groundNoise;

	return B.GetValue(x, y, z);
}

void Terrain::sample(
	float* values, 
	int32_t x, 
	int32_t y, 
	int32_t z, 
	int32_t x1, 
	int32_t y1, 
	int32_t z1, 
	float scale)
{
	//float* noise1 = fastNoise->GetSimplexFractalSet(x, 0, z, x1, 1, z1, scale);
	//float* noise2 = fastNoise2->GetCellularSet(x, y, z, x1, y1, z1, scale);
	float* noise3 = fastNoise3->GetSimplexFractalSet(x, y, z, x1, y1, z1, scale);
	//float* noise4 = fastNoise4->GetSimplexFractalSet(x, y, z, x1, y1, z1, scale);
	//float* noise5 = fastNoise5->GetSimplexFractalSet(x, y, z, x1, y1, z1, scale);

	const int count = x1 * y1 * z1;

	/*const int count = x1 * y1 * z1;
	for (int i = 0; i < count; ++i)
	{
		const int lx = i % x1;
		const int ly = (i / x1) % y1;
		const int lz = (i / x1) / y1;
		
		const int gx = x + lx;
		const int gy = y + ly;
		const int gz = z + lz;

		//values[i] = ((noise1[i] * noise2[i]) - (noise3[i] * noise4[i])) + (noise5[i] * 0.1f);
		//values[i] = (float)gy * 0.05f + noise1[i % (x1 * y1)];
		values[i] = noise3[i];
	}*/

	memcpy(values, noise3, sizeof(float) * count);

	//fastNoise->FreeNoiseSet(noise1);
	//fastNoise2->FreeNoiseSet(noise2);
	fastNoise3->FreeNoiseSet(noise3);
	//fastNoise4->FreeNoiseSet(noise4);
	//fastNoise5->FreeNoiseSet(noise5);
}