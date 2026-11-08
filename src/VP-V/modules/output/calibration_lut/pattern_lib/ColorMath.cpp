// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only

// The one out-of-line function of ColorMath.h (CIEDE2000). Standard library only.

#include "ColorMath.h"

#include <algorithm>
#include <cmath>

namespace Calibration
{
	namespace
	{
		const double Pi = 3.14159265358979323846;

		double Deg(double r) { return r * 180.0 / Pi; }
		double Rad(double d) { return d * Pi / 180.0; }
	}

	double DeltaE2000(const Vec3& lab1, const Vec3& lab2)
	{
		const double L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
		const double L2 = lab2[0], a2 = lab2[1], b2 = lab2[2];
		const double C1 = std::sqrt(a1 * a1 + b1 * b1), C2 = std::sqrt(a2 * a2 + b2 * b2);
		const double Cbar = 0.5 * (C1 + C2);
		const double Cbar7 = std::pow(Cbar, 7.0), p25 = std::pow(25.0, 7.0);
		const double G = 0.5 * (1.0 - std::sqrt(Cbar7 / (Cbar7 + p25)));
		const double a1p = (1.0 + G) * a1, a2p = (1.0 + G) * a2;
		const double C1p = std::sqrt(a1p * a1p + b1 * b1), C2p = std::sqrt(a2p * a2p + b2 * b2);
		auto hue = [](double b, double a)
		{
			if (a == 0.0 && b == 0.0) return 0.0;
			double h = Deg(std::atan2(b, a));
			if (h < 0.0) h += 360.0;
			return h;
		};
		const double h1p = hue(b1, a1p), h2p = hue(b2, a2p);
		const double dLp = L2 - L1, dCp = C2p - C1p;
		double dhp = 0.0;
		if (C1p * C2p != 0.0)
		{
			dhp = h2p - h1p;
			if (dhp > 180.0) dhp -= 360.0;
			else if (dhp < -180.0) dhp += 360.0;
		}
		const double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(Rad(dhp) / 2.0);
		const double Lbarp = 0.5 * (L1 + L2), Cbarp = 0.5 * (C1p + C2p);
		double hbarp = h1p + h2p;
		if (C1p * C2p != 0.0)
		{
			if (std::fabs(h1p - h2p) <= 180.0) hbarp = 0.5 * (h1p + h2p);
			else if (h1p + h2p < 360.0) hbarp = 0.5 * (h1p + h2p + 360.0);
			else hbarp = 0.5 * (h1p + h2p - 360.0);
		}
		const double T = 1.0 - 0.17 * std::cos(Rad(hbarp - 30.0)) + 0.24 * std::cos(Rad(2.0 * hbarp)) +
			0.32 * std::cos(Rad(3.0 * hbarp + 6.0)) - 0.20 * std::cos(Rad(4.0 * hbarp - 63.0));
		const double dTheta = 30.0 * std::exp(-((hbarp - 275.0) / 25.0) * ((hbarp - 275.0) / 25.0));
		const double Cbarp7 = std::pow(Cbarp, 7.0);
		const double Rc = 2.0 * std::sqrt(Cbarp7 / (Cbarp7 + p25));
		const double l50 = (Lbarp - 50.0) * (Lbarp - 50.0);
		const double Sl = 1.0 + 0.015 * l50 / std::sqrt(20.0 + l50);
		const double Sc = 1.0 + 0.045 * Cbarp;
		const double Sh = 1.0 + 0.015 * Cbarp * T;
		const double Rt = -std::sin(Rad(2.0 * dTheta)) * Rc;
		const double tl = dLp / Sl, tc = dCp / Sc, th = dHp / Sh;
		return std::sqrt((std::max)(0.0, tl * tl + tc * tc + th * th + Rt * tc * th));
	}
}
