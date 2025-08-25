#include <gtest/gtest.h>
#include "Engine.h"
#include <numeric>

using namespace MyCoreEngine;

TEST(CSMSplits, Monotonic_Endpoints) {
	const float n = 0.1f, f = 1000.0f;
	for (int casc = 1; casc <= 6; ++casc) {
		for (float lambda : {0.0f, 0.3f, 0.7f, 1.0f}) {
			auto Z = ComputeCSMSplits(n, f, casc, lambda);
			ASSERT_EQ(int(Z.size()), casc + 1);
			EXPECT_NEAR(Z.front(), n, 1e-6f);
			EXPECT_NEAR(Z.back(), f, 1e-5f);
			for (int i = 0; i < casc; ++i) {
				EXPECT_LT(Z[i], Z[i + 1]) << "i=" << i << " lambda=" << lambda;
			}
		}
	}
}

TEST(CSMSplits, UniformVsLogarithmic) {
	const float n = 0.1f, f = 1000.0f;
	const int casc = 4;
	auto Zuni = ComputeCSMSplits(n, f, casc, 0.0f);
	auto Zlog = ComputeCSMSplits(n, f, casc, 1.0f);
	for (int i = 1; i <= casc; ++i) {
		const float s = float(i) / float(casc);
		const float uni = n + (f - n) * s;
		const float logv = n * std::pow(f / n, s);
		EXPECT_NEAR(Zuni[i], uni, 1e-5f);
		EXPECT_NEAR(Zlog[i], logv, 1e-5f);
		// For n << f, logarithmic splits lie closer to near than uniform:
		if (i < casc) {               // last element equals 'f'
			EXPECT_GT(Zuni[i], Zlog[i]);
		}
		else {
			EXPECT_EQ(Zuni[i], Zlog[i]); // both == f
		}
	}
}

TEST(CSMSplits, BlendIsBetweenUniformAndLog) {
	const float n = 0.1f, f = 1000.0f;
	const int casc = 4;
	auto Za = ComputeCSMSplits(n, f, casc, 0.7f);
	auto Z0 = ComputeCSMSplits(n, f, casc, 0.0f);
	auto Z1 = ComputeCSMSplits(n, f, casc, 1.0f);
	for (int i = 1; i <= casc; ++i) {
		// With Lerp(uni, log, λ), the blend lies between log and uniform.
		// Since log < uniform for i < casc, we expect: Z1 <= Za <= Z0.
		if (i < casc) {
			EXPECT_LE(Z1[i], Za[i]);
			EXPECT_LE(Za[i], Z0[i]);
		}
		else {
			// endpoints equal far plane
			EXPECT_EQ(Za[i], Z0[i]);
			EXPECT_EQ(Za[i], Z1[i]);
		}
	}
}

TEST(CSMSplits, ScaleInvariance) {
	const float n = 0.1f, f = 600.0f, k = 3.5f;
	const int casc = 3;
	for (float lambda : {0.0f, 0.5f, 1.0f}) {
		auto Z = ComputeCSMSplits(n, f, casc, lambda);
		auto Zk = ComputeCSMSplits(n * k, f * k, casc, lambda);
		ASSERT_EQ(Z.size(), Zk.size());
		for (size_t i = 0; i < Z.size(); ++i) {
			EXPECT_NEAR(Zk[i], Z[i] * k, std::max(1e-5f, 1e-6f * Zk[i]));
		}
	}
}