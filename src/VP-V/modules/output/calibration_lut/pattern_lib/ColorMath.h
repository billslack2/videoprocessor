// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration (the module README): the small,
// pure colour maths shared by the calibration core. Double precision
// throughout; no renderer, GPU or libplacebo state.

#include <array>
#include <cmath>

namespace Calibration
{
	typedef std::array<double, 3> Vec3;

	struct Mat3
	{
		// Row-major: m[row][column].
		double m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

		Vec3 operator*(const Vec3& v) const
		{
			return { m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
				m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
				m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] };
		}

		Mat3 operator*(const Mat3& o) const
		{
			Mat3 r;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
			return r;
		}

		Vec3 Column(int c) const { return { m[0][c], m[1][c], m[2][c] }; }
		void SetColumn(int c, const Vec3& v) { m[0][c] = v[0]; m[1][c] = v[1]; m[2][c] = v[2]; }

		double Determinant() const
		{
			return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
				m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
		}

		// False (and identity) when singular.
		bool Inverse(Mat3& out) const
		{
			const double det = Determinant();
			if (!(std::fabs(det) > 1e-300)) { out = Mat3(); return false; }
			const double inv = 1.0 / det;
			out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv;
			out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
			out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
			out.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv;
			out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
			out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
			out.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv;
			out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
			out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
			return true;
		}

		Mat3 Inverse() const { Mat3 r; Inverse(r); return r; }
	};

	inline Vec3 Add(const Vec3& a, const Vec3& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
	inline Vec3 Sub(const Vec3& a, const Vec3& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
	inline Vec3 Scale(const Vec3& a, double s) { return { a[0] * s, a[1] * s, a[2] * s }; }
	inline double Dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

	struct Chromaticity
	{
		double x = 0.0;
		double y = 0.0;
	};

	struct Primaries
	{
		Chromaticity red, green, blue, white;
	};

	namespace Gamuts
	{
		inline Chromaticity D65() { return { 0.3127, 0.3290 }; }
		inline Primaries Bt709() { return { { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 }, D65() }; }
		inline Primaries P3D65() { return { { 0.680, 0.320 }, { 0.265, 0.690 }, { 0.150, 0.060 }, D65() }; }
		inline Primaries Bt2020() { return { { 0.708, 0.292 }, { 0.170, 0.797 }, { 0.131, 0.046 }, D65() }; }
	}

	// XYZ of a chromaticity at luminance Y.
	inline Vec3 XyToXyz(const Chromaticity& c, double Y)
	{
		if (!(c.y > 0.0)) return { 0.0, 0.0, 0.0 };
		return { c.x * Y / c.y, Y, (1.0 - c.x - c.y) * Y / c.y };
	}

	inline Chromaticity XyzToXy(const Vec3& xyz)
	{
		const double s = xyz[0] + xyz[1] + xyz[2];
		if (!(s > 0.0)) return { 0.0, 0.0 };
		return { xyz[0] / s, xyz[1] / s };
	}

	// Linear RGB (white = 1,1,1 at Y = 1) to XYZ.
	inline Mat3 RgbToXyz(const Primaries& p)
	{
		Mat3 m;
		const Vec3 r = XyToXyz(p.red, 1.0), g = XyToXyz(p.green, 1.0), b = XyToXyz(p.blue, 1.0);
		m.SetColumn(0, r); m.SetColumn(1, g); m.SetColumn(2, b);
		const Vec3 s = m.Inverse() * XyToXyz(p.white, 1.0);
		m.SetColumn(0, Scale(r, s[0])); m.SetColumn(1, Scale(g, s[1])); m.SetColumn(2, Scale(b, s[2]));
		return m;
	}

	// CIE 1976 L*a*b* against a reference white (XYZ, any scale).
	inline double LabF(double t)
	{
		const double e = 216.0 / 24389.0, k = 24389.0 / 27.0;
		return t > e ? std::cbrt(t) : (k * t + 16.0) / 116.0;
	}

	inline double LabFInverse(double f)
	{
		const double e = 216.0 / 24389.0, k = 24389.0 / 27.0;
		const double f3 = f * f * f;
		return f3 > e ? f3 : (116.0 * f - 16.0) / k;
	}

	inline Vec3 XyzToLab(const Vec3& xyz, const Vec3& white)
	{
		const double fx = LabF(xyz[0] / white[0]), fy = LabF(xyz[1] / white[1]), fz = LabF(xyz[2] / white[2]);
		return { 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
	}

	inline Vec3 LabToXyz(const Vec3& lab, const Vec3& white)
	{
		const double fy = (lab[0] + 16.0) / 116.0;
		const double fx = fy + lab[1] / 500.0, fz = fy - lab[2] / 200.0;
		return { LabFInverse(fx) * white[0], LabFInverse(fy) * white[1], LabFInverse(fz) * white[2] };
	}

	// CIEDE2000 (Sharma, Wu, Dalal 2005), kL = kC = kH = 1.
	double DeltaE2000(const Vec3& lab1, const Vec3& lab2);

	// McCamy's approximation of the correlated colour temperature (K).
	inline double CctMcCamy(const Chromaticity& c)
	{
		const double n = (c.x - 0.3320) / (0.1858 - c.y);
		return 449.0 * n * n * n + 3525.0 * n * n + 6823.3 * n + 5520.33;
	}
}
