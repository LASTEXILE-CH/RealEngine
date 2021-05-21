#pragma once

#include "linalg/linalg.h"

using namespace linalg;
using namespace linalg::aliases;

using uint = uint32_t;

inline float degree_to_randian(float degree)
{
	const float PI = 3.141592653f;
	return degree * PI / 180.0f;
}

inline float3 degree_to_randian(float3 degree)
{
	const float PI = 3.141592653f;
	return degree * PI / 180.0f;
}

inline float radian_to_degree(float radian)
{
	const float PI = 3.141592653f;
	return radian * 180.0f / PI;
}

inline float3 radian_to_degree(float3 radian)
{
	const float PI = 3.141592653f;
	return radian * 180.0f / PI;
}