#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

#include "../src/core/Core.h"

namespace MyCoreEngine {
	
	// Simple lerp that works in C++17 (std::lerp is C++20)
	inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	
	// Returns split plane distances Z[0..N] in linear view space, with:
	// Z[0] = nearZ, Z[N] = farZ, and Z[i] strictly increasing.
	// lambda=0 → uniform, lambda=1 → logarithmic, 0..1 blend = practical CSM.
	ENGINE_API inline std::vector<float> ComputeCSMSplits(float nearZ, float farZ, int cascades, float lambda) {
		assert(nearZ > 0.0f && farZ > nearZ);
		cascades = std::max(1, cascades);
		lambda = std::max(0.0f, std::min(1.0f, lambda));
	
		std::vector<float> Z(cascades + 1);
		Z[0] = nearZ;
		const float range = farZ - nearZ;
		const float ratio = farZ / nearZ;  // >1
	
		for (int i = 1; i <= cascades; ++i) {
			const float s = float(i) / float(cascades); // (0,1]
			const float uni = nearZ + range * s;
			const float logv = nearZ * std::pow(ratio, s);
			Z[i] = Lerp(uni, logv, lambda);
		}
		return Z;
	}
} // namespace MyCoreEngine