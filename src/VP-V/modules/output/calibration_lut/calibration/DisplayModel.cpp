// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "DisplayModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace Calibration
{
	namespace
	{
		bool SameValue(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

		bool InUnitCube(const Vec3& d, double tol = 1e-9)
		{
			for (int k = 0; k < 3; ++k)
				if (d[k] < -tol || d[k] > 1.0 + tol) return false;
			return true;
		}

		Vec3 Clamp01(const Vec3& d)
		{
			return { (std::min)((std::max)(d[0], 0.0), 1.0), (std::min)((std::max)(d[1], 0.0), 1.0),
				(std::min)((std::max)(d[2], 0.0), 1.0) };
		}

		double Norm(const Vec3& v) { return std::sqrt(Dot(v, v)); }

		// Solve a 3x3 system (row-major a), false when singular.
		bool Solve3(const double a[3][3], const double b[3], double x[3])
		{
			Mat3 m;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					m.m[i][j] = a[i][j];
			Mat3 inv;
			if (!m.Inverse(inv)) return false;
			const Vec3 r = inv * Vec3{ b[0], b[1], b[2] };
			x[0] = r[0]; x[1] = r[1]; x[2] = r[2];
			return std::isfinite(x[0]) && std::isfinite(x[1]) && std::isfinite(x[2]);
		}

		// Isotonic (non-decreasing) regression, pool adjacent violators.
		std::vector<double> Isotonic(const std::vector<double>& y, const std::vector<double>& w)
		{
			struct Block { double sum, weight; size_t count; };
			std::vector<Block> blocks;
			for (size_t i = 0; i < y.size(); ++i)
			{
				blocks.push_back({ y[i] * w[i], w[i], 1 });
				while (blocks.size() >= 2)
				{
					Block& b = blocks[blocks.size() - 1];
					Block& a = blocks[blocks.size() - 2];
					if (a.sum / a.weight <= b.sum / b.weight) break;
					a.sum += b.sum; a.weight += b.weight; a.count += b.count;
					blocks.pop_back();
				}
			}
			std::vector<double> out;
			out.reserve(y.size());
			for (const Block& b : blocks)
				for (size_t i = 0; i < b.count; ++i)
					out.push_back(b.sum / b.weight);
			return out;
		}
	}

	// -------------------------------------------------------- MonotoneSpline

	bool MonotoneSpline::Fit(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>* weights)
	{
		xs.clear(); ys.clear(); ms.clear();
		if (x.size() != y.size() || x.size() < 2) return false;
		// Sort, merge equal x (weighted mean).
		std::map<double, std::pair<double, double>> merged;
		for (size_t i = 0; i < x.size(); ++i)
		{
			if (!std::isfinite(x[i]) || !std::isfinite(y[i])) continue;
			const double w = weights ? (std::max)((*weights)[i], 1e-12) : 1.0;
			bool found = false;
			for (auto& entry : merged)
				if (SameValue(entry.first, x[i], 1e-12))
				{
					entry.second.first += w * y[i];
					entry.second.second += w;
					found = true;
					break;
				}
			if (!found) merged[x[i]] = std::make_pair(w * y[i], w);
		}
		if (merged.size() < 2) return false;
		std::vector<double> mx, my, mw;
		for (const auto& entry : merged)
		{
			mx.push_back(entry.first);
			my.push_back(entry.second.first / entry.second.second);
			mw.push_back(entry.second.second);
		}
		xs = mx;
		ys = Isotonic(my, mw);

		// PCHIP tangents (Fritsch-Butland weighted harmonic mean): monotone.
		const size_t n = xs.size();
		std::vector<double> h(n - 1), delta(n - 1);
		for (size_t i = 0; i + 1 < n; ++i)
		{
			h[i] = xs[i + 1] - xs[i];
			delta[i] = (ys[i + 1] - ys[i]) / h[i];
		}
		ms.assign(n, 0.0);
		if (n == 2)
		{
			ms[0] = ms[1] = delta[0];
			return true;
		}
		for (size_t i = 1; i + 1 < n; ++i)
		{
			if (delta[i - 1] * delta[i] <= 0.0) { ms[i] = 0.0; continue; }
			const double w1 = 2.0 * h[i] + h[i - 1], w2 = h[i] + 2.0 * h[i - 1];
			ms[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
		}
		auto end = [](double h0, double h1, double d0, double d1)
		{
			double m = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
			if (m * d0 <= 0.0) m = 0.0;
			else if (d0 * d1 <= 0.0 && std::fabs(m) > 3.0 * std::fabs(d0)) m = 3.0 * d0;
			return m;
		};
		ms[0] = end(h[0], h[1], delta[0], delta[1]);
		ms[n - 1] = end(h[n - 2], h[n - 3], delta[n - 2], delta[n - 3]);
		return true;
	}

	double MonotoneSpline::Eval(double x) const
	{
		if (xs.empty()) return x;
		const size_t n = xs.size();
		if (x <= xs.front()) return ys.front() + ms.front() * (x - xs.front());
		if (x >= xs.back()) return ys.back() + ms.back() * (x - xs.back());
		const size_t i = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin()) - 1;
		const size_t j = (std::min)(i + 1, n - 1);
		const double h = xs[j] - xs[i];
		const double t = (x - xs[i]) / h;
		const double t2 = t * t, t3 = t2 * t;
		return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * ms[i] + (-2 * t3 + 3 * t2) * ys[j] +
			(t3 - t2) * h * ms[j];
	}

	double MonotoneSpline::Slope(double x) const
	{
		if (xs.empty()) return 1.0;
		const size_t n = xs.size();
		if (x <= xs.front()) return ms.front();
		if (x >= xs.back()) return ms.back();
		const size_t i = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin()) - 1;
		const size_t j = (std::min)(i + 1, n - 1);
		const double h = xs[j] - xs[i];
		const double t = (x - xs[i]) / h;
		const double t2 = t * t;
		return ((6 * t2 - 6 * t) * ys[i] + (3 * t2 - 4 * t + 1) * h * ms[i] + (-6 * t2 + 6 * t) * ys[j] +
			(3 * t2 - 2 * t) * h * ms[j]) / h;
	}

	double MonotoneSpline::Inverse(double y, double lo, double hi) const
	{
		if (Eval(lo) >= y) return lo;
		if (Eval(hi) < y) return hi;
		for (int i = 0; i < 80; ++i)
		{
			const double mid = 0.5 * (lo + hi);
			if (Eval(mid) >= y) hi = mid; else lo = mid;
		}
		return 0.5 * (lo + hi);
	}

	// ---------------------------------------------------------- DisplayModel

	bool DisplayModel::Fit(const std::vector<Measurement>& m, const ForwardModelOptions& options, std::string* error)
	{
		valid = false;
		auto fail = [error](const char* text) { if (error) *error = text; return false; };

		auto average = [&m](const Vec3& device, bool& found)
		{
			Vec3 sum = { 0, 0, 0 };
			int count = 0;
			for (const Measurement& x : m)
				if (SameValue(x.device[0], device[0]) && SameValue(x.device[1], device[1]) && SameValue(x.device[2], device[2]))
				{
					sum = Add(sum, x.xyz);
					++count;
				}
			found = count > 0;
			return count ? Scale(sum, 1.0 / count) : sum;
		};
		bool fb = false, fr = false, fg = false, fbl = false, fw = false;
		black = average({ 0, 0, 0 }, fb);
		const Vec3 red = average({ 1, 0, 0 }, fr), green = average({ 0, 1, 0 }, fg), blue = average({ 0, 0, 1 }, fbl);
		const Vec3 measuredWhite = average({ 1, 1, 1 }, fw);
		if (!fb || !fr || !fg || !fbl || !fw) return fail("need black, white and the full red, green and blue patches");

		primaryXyz[0] = Sub(red, black);
		primaryXyz[1] = Sub(green, black);
		primaryXyz[2] = Sub(blue, black);
		matrix.SetColumn(0, Sub(red, black));
		matrix.SetColumn(1, Sub(green, black));
		matrix.SetColumn(2, Sub(blue, black));
		if (!matrix.Inverse(matrixInverse)) return fail("the primaries are degenerate");
		const Vec3 sum = Sub(Add(Add(red, green), blue), Scale(black, 2.0));
		additivity = measuredWhite[1] > 0.0 ? std::fabs(measuredWhite[1] - sum[1]) / measuredWhite[1] : 0.0;
		whiteY = (std::max)(measuredWhite[1], 1e-12);
		epsilon = (std::max)(options.darkEpsilon, 1e-9) * whiteY;

		std::vector<const Measurement*> greys;
		for (const Measurement& x : m)
			if (SameValue(x.device[0], x.device[1]) && SameValue(x.device[1], x.device[2]))
				greys.push_back(&x);
		if (greys.size() < 3) return fail("need a greyscale");

		auto fitCurves = [&]() -> bool
		{
			std::vector<double> t, f[3];
			for (const Measurement* g : greys)
			{
				const Vec3 c = matrixInverse * Sub(g->xyz, black);
				t.push_back(g->device[0]);
				for (int k = 0; k < 3; ++k) f[k].push_back(c[k]);
			}
			for (int k = 0; k < 3; ++k)
			{
				if (!curves[k].Fit(t, f[k])) return false;
				// Gauge: f(1) = 1, the scale goes to the matrix column.
				const double s = curves[k].Eval(1.0);
				if (!(s > 1e-9)) return false;
				for (double& v : f[k]) v /= s;
				if (!curves[k].Fit(t, f[k])) return false;
				matrix.SetColumn(k, Scale(matrix.Column(k), s));
			}
			return matrix.Inverse(matrixInverse);
		};
		auto fitMatrix = [&]() -> bool
		{
			double a[3][3] = {}, b[3][3] = {};
			int used = 0;
			for (const Measurement& x : m)
			{
				if (!InUnitCube(x.device)) continue;
				const Vec3 f = { curves[0].Eval(x.device[0]), curves[1].Eval(x.device[1]), curves[2].Eval(x.device[2]) };
				const Vec3 y = Sub(x.xyz, black);
				const double w = 1.0 / ((std::max)(x.xyz[1], 0.0) + epsilon) / ((std::max)(x.xyz[1], 0.0) + epsilon);
				for (int i = 0; i < 3; ++i)
					for (int j = 0; j < 3; ++j)
					{
						a[i][j] += w * f[i] * f[j];
						b[i][j] += w * y[i] * f[j];   // b[row k][j]
					}
				++used;
			}
			if (used < 6) return true;
			Mat3 next;
			for (int k = 0; k < 3; ++k)
			{
				double x[3];
				if (!Solve3(a, b[k], x)) return true;
				next.m[k][0] = x[0]; next.m[k][1] = x[1]; next.m[k][2] = x[2];
			}
			Mat3 inv;
			if (!next.Inverse(inv)) return true;
			matrix = next;
			matrixInverse = inv;
			return true;
		};

		if (!fitCurves()) return fail("the greyscale could not be fitted");
		for (int i = 0; i < (std::max)(0, options.alternations); ++i)
		{
			fitMatrix();
			if (!fitCurves()) return fail("the greyscale could not be fitted");
		}

		minDevice = curves[0].MinX();
		maxDevice = curves[0].MaxX();
		for (int k = 0; k < 3; ++k)
		{
			// Where the curve starts to rise (BTB-clipped: about 0; lifted: below).
			const double bottom = curves[k].Eval(minDevice);
			const double span = curves[k].Eval(1.0) - bottom;
			responsiveFrom[k] = curves[k].Inverse(bottom + 1e-9 * (std::max)(span, 1e-12), minDevice, maxDevice);
		}
		valid = true;
		FitResidual(m, options);
		return true;
	}

	Vec3 DisplayModel::Linear(const Vec3& d) const
	{
		const Vec3 f = { curves[0].Eval(d[0]), curves[1].Eval(d[1]), curves[2].Eval(d[2]) };
		return Add(black, matrix * f);
	}

	Vec3 DisplayModel::Residual(const Vec3& d) const
	{
		if (grid < 2 || field.empty()) return { 0, 0, 0 };
		int i0[3];
		double t[3];
		for (int k = 0; k < 3; ++k)
		{
			const double p = (std::min)((std::max)(d[k], 0.0), 1.0) * (grid - 1);
			i0[k] = (std::min)(static_cast<int>(std::floor(p)), grid - 2);
			t[k] = p - i0[k];
		}
		Vec3 r = { 0, 0, 0 };
		for (int c = 0; c < 8; ++c)
		{
			const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
			const double w = (dx ? t[0] : 1 - t[0]) * (dy ? t[1] : 1 - t[1]) * (dz ? t[2] : 1 - t[2]);
			if (w == 0.0) continue;
			const Vec3& v = field[(static_cast<size_t>(i0[2] + dz) * grid + (i0[1] + dy)) * grid + (i0[0] + dx)];
			r = Add(r, Scale(v, w));
		}
		return r;
	}

	Vec3 DisplayModel::Forward(const Vec3& d) const
	{
		const Vec3 lin = Linear(d);
		if (grid < 2) return lin;
		return Add(lin, Scale(Residual(d), (std::max)(lin[1], 0.0) + epsilon));
	}

	void DisplayModel::FitResidual(const std::vector<Measurement>& m, const ForwardModelOptions& options)
	{
		grid = 0;
		field.clear();
		struct Point { int index[8]; double weight[8]; Vec3 r; };
		std::vector<Point> points;
		int cubeCount = 0;
		for (const Measurement& x : m)
			if (x.kind == PatchKind::Cube && InUnitCube(x.device)) ++cubeCount;
		int g = options.residualGrid;
		if (g < 0)
		{
			const int n = static_cast<int>(std::floor(std::cbrt(static_cast<double>(cubeCount)) + 0.5));
			g = n >= 3 ? (std::min)(n, 17) : 0;
		}
		if (g < 2) return;

		for (const Measurement& x : m)
		{
			if (!InUnitCube(x.device)) continue;
			const Vec3 lin = Linear(x.device);
			const double s = (std::max)(lin[1], 0.0) + epsilon;
			Point p;
			p.r = Scale(Sub(x.xyz, lin), 1.0 / s);
			int i0[3];
			double t[3];
			for (int k = 0; k < 3; ++k)
			{
				const double q = (std::min)((std::max)(x.device[k], 0.0), 1.0) * (g - 1);
				i0[k] = (std::min)(static_cast<int>(std::floor(q)), g - 2);
				t[k] = q - i0[k];
			}
			for (int c = 0; c < 8; ++c)
			{
				const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
				p.weight[c] = (dx ? t[0] : 1 - t[0]) * (dy ? t[1] : 1 - t[1]) * (dz ? t[2] : 1 - t[2]);
				p.index[c] = ((i0[2] + dz) * g + (i0[1] + dy)) * g + (i0[0] + dx);
			}
			points.push_back(p);
		}
		if (points.size() < 8) return;

		const int nodes = g * g * g;
		const double scale = static_cast<double>(points.size()) / nodes;
		const double lambda = (std::max)(0.0, options.smoothness) * scale;
		const double rho = (std::max)(0.0, options.ridge) * scale;

		auto apply = [&](const std::vector<double>& x, std::vector<double>& y)
		{
			std::fill(y.begin(), y.end(), 0.0);
			for (const Point& p : points)
			{
				double v = 0.0;
				for (int c = 0; c < 8; ++c) v += p.weight[c] * x[p.index[c]];
				for (int c = 0; c < 8; ++c) y[p.index[c]] += p.weight[c] * v;
			}
			// Second differences along each axis (bending energy).
			const int stride[3] = { 1, g, g * g };
			for (int axis = 0; axis < 3; ++axis)
				for (int z = 0; z < g; ++z)
					for (int yy = 0; yy < g; ++yy)
						for (int xx = 0; xx < g; ++xx)
						{
							const int coord[3] = { xx, yy, z };
							if (coord[axis] == 0 || coord[axis] == g - 1) continue;
							const int i = (z * g + yy) * g + xx;
							const double s = x[i - stride[axis]] - 2.0 * x[i] + x[i + stride[axis]];
							y[i - stride[axis]] += lambda * s;
							y[i] -= 2.0 * lambda * s;
							y[i + stride[axis]] += lambda * s;
						}
			for (int i = 0; i < nodes; ++i) y[i] += rho * x[i];
		};

		field.assign(nodes, Vec3{ 0, 0, 0 });
		for (int comp = 0; comp < 3; ++comp)
		{
			std::vector<double> b(nodes, 0.0), x(nodes, 0.0), r, p, ap(nodes);
			for (const Point& pt : points)
				for (int c = 0; c < 8; ++c) b[pt.index[c]] += pt.weight[c] * pt.r[comp];
			r = b;
			p = r;
			double rr = 0.0, bb = 0.0;
			for (int i = 0; i < nodes; ++i) { rr += r[i] * r[i]; bb += b[i] * b[i]; }
			if (bb <= 0.0) continue;
			for (int it = 0; it < 4 * nodes && rr > 1e-24 * bb; ++it)
			{
				apply(p, ap);
				double pap = 0.0;
				for (int i = 0; i < nodes; ++i) pap += p[i] * ap[i];
				if (!(pap > 0.0)) break;
				const double alpha = rr / pap;
				double rrNext = 0.0;
				for (int i = 0; i < nodes; ++i)
				{
					x[i] += alpha * p[i];
					r[i] -= alpha * ap[i];
					rrNext += r[i] * r[i];
				}
				const double beta = rrNext / rr;
				rr = rrNext;
				for (int i = 0; i < nodes; ++i) p[i] = r[i] + beta * p[i];
			}
			for (int i = 0; i < nodes; ++i) field[i][comp] = x[i];
		}
		grid = g;
	}

	bool DisplayModel::ChannelResponds(int c, double from, double to) const
	{
		const double a = (std::min)(from, to), b = (std::max)(from, to);
		if (b - a < 1e-9) return true;
		const double slope = (curves[c].Eval(b) - curves[c].Eval(a)) / (b - a);
		double r0 = 0.0, r1 = 1.0;
		if (b <= 1e-9) { r0 = 0.0; r1 = 0.1; }
		else if (a >= 1.0 - 1e-9) { r0 = 0.9; r1 = 1.0; }
		const double reference = (curves[c].Eval(r1) - curves[c].Eval(r0)) / (r1 - r0);
		return slope > 0.0 && slope > 0.05 * reference;
	}

	double DisplayModel::Refine(const Vec3& xyz, Vec3& d, double tolerance) const
	{
		const double hi = maxDevice + 0.25;
		const double scale = (std::max)(xyz[1], 0.0) + epsilon;
		auto error = [&](const Vec3& p, Vec3& e)
		{
			e = Scale(Sub(xyz, Forward(p)), 1.0 / scale);
			return Norm(e);
		};
		for (int k = 0; k < 3; ++k) d[k] = (std::min)((std::max)(d[k], responsiveFrom[k]), hi);
		Vec3 e;
		double err = error(d, e);
		double mu = 1e-6;
		const double h = 1e-6;
		for (int it = 0; it < 60 && err > tolerance; ++it)
		{
			double J[3][3];
			for (int j = 0; j < 3; ++j)
			{
				Vec3 p1 = d, p0 = d;
				p1[j] += h; p0[j] -= h;
				const Vec3 col = Scale(Sub(Forward(p1), Forward(p0)), 1.0 / (2.0 * h * scale));
				for (int i = 0; i < 3; ++i) J[i][j] = col[i];
			}
			double A[3][3], g[3];
			for (int i = 0; i < 3; ++i)
			{
				g[i] = J[0][i] * e[0] + J[1][i] * e[1] + J[2][i] * e[2];
				for (int j = 0; j < 3; ++j)
					A[i][j] = J[0][i] * J[0][j] + J[1][i] * J[1][j] + J[2][i] * J[2][j];
			}
			bool accepted = false;
			for (int tries = 0; tries < 14 && !accepted; ++tries)
			{
				double Ad[3][3];
				for (int i = 0; i < 3; ++i)
					for (int j = 0; j < 3; ++j)
						Ad[i][j] = A[i][j] + (i == j ? mu * (A[i][i] + 1e-12) : 0.0);
				double step[3];
				if (Solve3(Ad, g, step))
				{
					Vec3 next = { d[0] + step[0], d[1] + step[1], d[2] + step[2] };
					// Iterates stay where the curves respond (a flat BTB part
					// has no gradient); a target needing less is unreachable.
					for (int k = 0; k < 3; ++k) next[k] = (std::min)((std::max)(next[k], responsiveFrom[k]), hi);
					Vec3 en;
					const double errNext = error(next, en);
					if (errNext < err)
					{
						d = next; e = en; err = errNext;
						mu = (std::max)(mu / 5.0, 1e-12);
						accepted = true;
						break;
					}
				}
				mu *= 8.0;
			}
			if (!accepted) break;
		}
		return err;
	}

	bool DisplayModel::Inverse(const Vec3& xyz, Vec3& device, const Vec3* start, double tolerance) const
	{
		if (!valid) return false;
		const double hi = maxDevice + 0.25;
		// Starts: the warm start, the linear model's inverse (kept off the
		// curves' flat toe), mid grey. The best result wins.
		Vec3 starts[3];
		int count = 0;
		if (start) starts[count++] = *start;
		const Vec3 g = matrixInverse * Sub(xyz, black);
		Vec3 linear;
		for (int k = 0; k < 3; ++k)
			linear[k] = (std::max)(responsiveFrom[k] + 0.01, curves[k].Inverse(g[k], responsiveFrom[k], hi));
		starts[count++] = linear;
		starts[count++] = { 0.5, 0.5, 0.5 };
		double best = 1e300;
		Vec3 bestD = starts[0];
		for (int i = 0; i < count; ++i)
		{
			Vec3 d = starts[i];
			const double err = Refine(xyz, d, tolerance);
			if (err < best) { best = err; bestD = d; }
			if (best <= tolerance) break;
		}
		device = bestD;
		return best <= tolerance;
	}

	// ----------------------------------------------------- CalibrationTarget

	bool CalibrationTarget::Build(const DisplayModel& model, const TargetOptions& opts, std::string* error)
	{
		options = opts;
		if (!model.Valid()) { if (error) *error = "no display model"; return false; }
		// The model's black (what device 0 reaches), not the raw reading.
		black = model.Forward({ 0.0, 0.0, 0.0 });
		// The largest white at the target white point the display reaches.
		const double nativeY = model.NativeWhite()[1];
		double lo = 0.0, hi = nativeY * 1.25;
		Vec3 d = { 1, 1, 1 };
		for (int i = 0; i < 60; ++i)
		{
			const double mid = 0.5 * (lo + hi);
			Vec3 trial;
			const bool ok = model.Inverse(XyToXyz(options.primaries.white, mid), trial, nullptr, 1e-9) &&
				trial[0] <= 1.0 && trial[1] <= 1.0 && trial[2] <= 1.0;
			if (ok) { lo = mid; d = trial; } else hi = mid;
		}
		if (!(lo > black[1])) { if (error) *error = "no reachable white above black"; return false; }
		const double y = lo * (std::min)((std::max)(options.whiteFraction, 1e-3), 1.0);
		white = XyToXyz(options.primaries.white, y);
		lb = black[1];
		lw = white[1];

		const Mat3 rgb = RgbToXyz(options.primaries);
		const Vec3 above = Sub(white, black);
		const Vec3 s = rgb.Inverse() * above;
		matrix = rgb;
		for (int c = 0; c < 3; ++c) matrix.SetColumn(c, Scale(rgb.Column(c), s[c]));

		const double g = (std::max)(options.gamma, 0.1);
		const double q = std::pow(lw, 1.0 / g) - std::pow((std::max)(lb, 0.0), 1.0 / g);
		bt1886A = std::pow(q, g);
		bt1886B = std::pow((std::max)(lb, 0.0), 1.0 / g) / q;
		return true;
	}

	double CalibrationTarget::RelativeLinear(double v) const
	{
		const double g = (std::max)(options.gamma, 0.1);
		if (options.transfer == TargetTransfer::Gamma)
			return v < 0.0 ? -std::pow(-v, g) : std::pow(v, g);
		const double x = v + bt1886B;
		const double l = x < 0.0 ? -bt1886A * std::pow(-x, g) : bt1886A * std::pow(x, g);
		return (l - lb) / (lw - lb);
	}

	Vec3 CalibrationTarget::Xyz(const Vec3& signal) const
	{
		const Vec3 lin = { RelativeLinear(signal[0]), RelativeLinear(signal[1]), RelativeLinear(signal[2]) };
		return Add(black, matrix * lin);
	}

	// -------------------------------------------------------- LUT generation

	bool MapTargetToDevice(const DisplayModel& model, const CalibrationTarget& target, const Vec3& signal,
		Vec3& device, bool* clipped, const Vec3* warm, int clipIterations)
	{
		if (clipped) *clipped = false;
		const Vec3 xyz = target.Xyz(Clamp01(signal));
		Vec3 d;
		if ((model.Inverse(xyz, d, warm) && InUnitCube(d, 1e-7)) ||
			(warm && model.Inverse(xyz, d, nullptr) && InUnitCube(d, 1e-7)))
		{
			device = Clamp01(d);
			return true;
		}
		if (clipped) *clipped = true;
		// LCh: keep L and h, shrink C until reachable. The neutral is the
		// target's own grey at the same luminance (the display's black
		// included, as the target's greyscale is), so L stays exact and the
		// hue is taken about the axis the display can actually show.
		const Vec3 white = target.White();
		const Vec3 lab = XyzToLab(xyz, white);
		const Vec3 above = Sub(white, target.Black());
		const double lin = above[1] > 0.0 ? (std::max)(0.0, (xyz[1] - target.Black()[1]) / above[1]) : 0.0;
		const Vec3 grey = Add(target.Black(), Scale(above, lin));
		const Vec3 labGrey = XyzToLab(grey, white);
		Vec3 dLo;
		if (!model.Inverse(grey, dLo, nullptr) || !InUnitCube(dLo, 1e-6))
		{
			device = Clamp01(d);
			return false;
		}
		double kLo = 0.0, kHi = 1.0;
		for (int i = 0; i < clipIterations; ++i)
		{
			const double k = 0.5 * (kLo + kHi);
			Vec3 trial;
			const Vec3 x = LabToXyz({ lab[0], labGrey[1] + k * (lab[1] - labGrey[1]), labGrey[2] + k * (lab[2] - labGrey[2]) }, white);
			if (model.InverseFrom(x, trial, dLo) && InUnitCube(trial, 1e-7)) { kLo = k; dLo = trial; }
			else kHi = k;
		}
		device = Clamp01(dLo);
		return true;
	}

	Vec3 Lut3D::Sample(const Vec3& signal) const
	{
		const int n = size;
		int i0[3];
		double t[3];
		for (int k = 0; k < 3; ++k)
		{
			const double p = (std::min)((std::max)(signal[k], 0.0), 1.0) * (n - 1);
			i0[k] = (std::min)(static_cast<int>(std::floor(p)), n - 2);
			t[k] = p - i0[k];
		}
		Vec3 r = { 0, 0, 0 };
		for (int c = 0; c < 8; ++c)
		{
			const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
			const double w = (dx ? t[0] : 1 - t[0]) * (dy ? t[1] : 1 - t[1]) * (dz ? t[2] : 1 - t[2]);
			if (w != 0.0) r = Add(r, Scale(At(i0[0] + dx, i0[1] + dy, i0[2] + dz), w));
		}
		return r;
	}

	namespace
	{
		// The neutral-axis slope beyond one end of the signal range from the
		// model's extended (BTB / WTW) response; false where it does not respond.
		bool FitEndSlope(const DisplayModel& model, const CalibrationTarget& target, const Vec3& corner, bool upper,
			Vec3& slope)
		{
			double deltaMax = 0.04;
			for (int attempt = 0; attempt < 4; ++attempt, deltaMax *= 0.5)
			{
				double sxx = 0.0;
				Vec3 sxy = { 0, 0, 0 };
				bool ok = true;
				Vec3 warm = corner;
				for (int j = 1; j <= 4 && ok; ++j)
				{
					const double delta = deltaMax * j / 4.0;
					const double v = upper ? 1.0 + delta : -delta;
					Vec3 d;
					if (!model.Inverse(target.Xyz({ v, v, v }), d, &warm, 1e-8)) { ok = false; break; }
					for (int k = 0; k < 3; ++k)
					{
						if (d[k] > model.MaxDevice() + 1e-6 || d[k] < model.MinDevice() - 1e-6) ok = false;
						if (upper && d[k] > 1.0 && !model.ChannelResponds(k, 1.0, d[k])) ok = false;
						if (!upper && d[k] < 0.0 && !model.ChannelResponds(k, d[k], 0.0)) ok = false;
					}
					const double x = upper ? delta : -delta;
					sxx += x * x;
					for (int k = 0; k < 3; ++k) sxy[k] += x * (d[k] - corner[k]);
					warm = d;
				}
				if (ok && sxx > 0.0)
				{
					slope = Scale(sxy, 1.0 / sxx);
					return true;
				}
			}
			return false;
		}
	}

	Lut3D BuildLut(const DisplayModel& model, const CalibrationTarget& target, const LutOptions& options)
	{
		Lut3D lut;
		const int n = (std::min)((std::max)(options.size, 2), 65);
		lut.size = n;
		lut.data.assign(static_cast<size_t>(n) * n * n * 3, 0.0f);
		for (int b = 0; b < n; ++b)
			for (int g = 0; g < n; ++g)
			{
				Vec3 warm;
				bool haveWarm = false;
				for (int r = 0; r < n; ++r)
				{
					const Vec3 v = { static_cast<double>(r) / (n - 1), static_cast<double>(g) / (n - 1),
						static_cast<double>(b) / (n - 1) };
					Vec3 d;
					bool clipped = false;
					const bool ok = MapTargetToDevice(model, target, v, d, &clipped, haveWarm ? &warm : nullptr,
						options.clipIterations);
					if (clipped) ++lut.clippedNodes;
					if (!ok) ++lut.failedNodes;
					lut.Set(r, g, b, d);
					warm = d;
					haveWarm = ok;
				}
			}

		if (options.monotoneNeutral)
			for (int k = 0; k < 3; ++k)
			{
				std::vector<double> y(n), w(n, 1.0);
				for (int i = 0; i < n; ++i) y[i] = lut.At(i, i, i)[k];
				const std::vector<double> fixed = Isotonic(y, w);
				for (int i = 0; i < n; ++i)
					if (std::fabs(fixed[i] - y[i]) > 1e-9)
					{
						Vec3 v = lut.At(i, i, i);
						v[k] = fixed[i];
						lut.Set(i, i, i, v);
						lut.neutralAdjusted = true;
					}
			}

		// End slopes (ExtendedRange::LutExtrapolate: corner + sum over clamped
		// axes of the edge cell's slope).
		const int last = n - 1;
		const double step = static_cast<double>(last);
		for (int upperEnd = 0; upperEnd < 2; ++upperEnd)
		{
			const bool upper = upperEnd == 1;
			const int c = upper ? last : 0;
			const int inner = upper ? last - 1 : 1;
			const Vec3 corner = lut.At(c, c, c);
			Vec3 axisSlope[3];
			Vec3 natural = { 0, 0, 0 };
			for (int a = 0; a < 3; ++a)
			{
				int idx[3] = { c, c, c };
				idx[a] = inner;
				const Vec3 neighbour = lut.At(idx[0], idx[1], idx[2]);
				axisSlope[a] = Scale(upper ? Sub(corner, neighbour) : Sub(neighbour, corner), step);
				natural = Add(natural, axisSlope[a]);
			}
			(upper ? lut.endSlopes.naturalHigh : lut.endSlopes.naturalLow) = natural;
			Vec3 wanted;
			if (!options.fitEndSlopes || !FitEndSlope(model, target, corner, upper, wanted))
			{
				(upper ? lut.endSlopes.slopeHigh : lut.endSlopes.slopeLow) = natural;
				continue;
			}
			(upper ? lut.endSlopes.fittedHigh : lut.endSlopes.fittedLow) = true;
			(upper ? lut.endSlopes.slopeHigh : lut.endSlopes.slopeLow) = wanted;
			const Vec3 change = Sub(wanted, natural);
			for (int a = 0; a < 3; ++a)
			{
				Vec3 s = axisSlope[a];
				for (int k = 0; k < 3; ++k)
				{
					const double total = axisSlope[0][k] + axisSlope[1][k] + axisSlope[2][k];
					const double share = std::fabs(total) > 1e-6 ? axisSlope[a][k] / total : 1.0 / 3.0;
					s[k] += change[k] * share;
				}
				int idx[3] = { c, c, c };
				idx[a] = inner;
				Vec3 neighbour = upper ? Sub(corner, Scale(s, 1.0 / step)) : Add(corner, Scale(s, 1.0 / step));
				for (int k = 0; k < 3; ++k) neighbour[k] = (std::min)((std::max)(neighbour[k], -0.5), 1.5);
				lut.Set(idx[0], idx[1], idx[2], neighbour);
			}
		}
		return lut;
	}

	std::string FormatCube(const Lut3D& lut, const std::string& title, const std::vector<std::string>& comments)
	{
		std::string out;
		std::string safeTitle = title;
		std::replace(safeTitle.begin(), safeTitle.end(), '"', '\'');
		out += "TITLE \"" + safeTitle + "\"\n";
		for (const std::string& comment : comments)
		{
			std::string line = comment;
			std::replace(line.begin(), line.end(), '\n', ' ');
			std::replace(line.begin(), line.end(), '\r', ' ');
			out += "# " + line + "\n";
		}
		out += "LUT_3D_SIZE " + std::to_string(lut.size) + "\n";
		char text[96];
		const size_t count = lut.data.size() / 3;
		out.reserve(out.size() + count * 30);
		for (size_t i = 0; i < count; ++i)
		{
			std::snprintf(text, sizeof(text), "%.7f %.7f %.7f\n", lut.data[3 * i], lut.data[3 * i + 1], lut.data[3 * i + 2]);
			out += text;
		}
		return out;
	}

	bool WriteCube(const Lut3D& lut, const std::string& path, const std::string& title,
		const std::vector<std::string>& comments, std::string* error)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) { if (error) *error = "cannot open " + path; return false; }
		const std::string text = FormatCube(lut, title, comments);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!file) { if (error) *error = "cannot write " + path; return false; }
		return true;
	}

	// ---------------------------------------------------------------- Verify

	VerifyReport BuildVerifyReport(const std::vector<VerifyEntry>& entries, const Vec3& targetWhite,
		const Vec3& targetBlack, const std::vector<std::pair<double, Vec3>>& greys)
	{
		VerifyReport report;
		report.entries = entries;
		std::vector<double> values;
		double sum = 0.0;
		for (VerifyEntry& e : report.entries)
		{
			e.deltaE = DeltaE2000(XyzToLab(e.targetXyz, targetWhite), XyzToLab(e.measuredXyz, targetWhite));
			values.push_back(e.deltaE);
			sum += e.deltaE;
			if (e.deltaE > report.maxDeltaE) { report.maxDeltaE = e.deltaE; report.worstLabel = e.label; }
		}
		if (!values.empty())
		{
			report.meanDeltaE = sum / values.size();
			std::sort(values.begin(), values.end());
			const size_t i = static_cast<size_t>(std::ceil(0.95 * values.size())) - 1;
			report.p95DeltaE = values[(std::min)(i, values.size() - 1)];
		}
		report.pass = !values.empty() && report.meanDeltaE < report.passMean && report.maxDeltaE < report.passMax;

		const double range = targetWhite[1] - targetBlack[1];
		for (const auto& g : greys)
		{
			GreyStep s;
			s.signal = g.first;
			s.luminance = g.second[1];
			const double rel = range > 0.0 ? (g.second[1] - targetBlack[1]) / range : 0.0;
			if (g.first > 0.0 && g.first < 1.0 && rel > 0.0) s.gamma = std::log(rel) / std::log(g.first);
			if (g.second[1] > 0.0) s.cct = CctMcCamy(XyzToXy(g.second));
			report.greys.push_back(s);
		}
		return report;
	}

	std::string FormatVerifyJson(const VerifyReport& report)
	{
		auto escape = [](const std::string& s)
		{
			std::string o;
			for (const char c : s)
			{
				if (c == '"' || c == '\\') { o += '\\'; o += c; }
				else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
				else o += c;
			}
			return o;
		};
		std::ostringstream out;
		out.precision(6);
		out << "{\n  \"mean_de2000\": " << report.meanDeltaE << ",\n  \"max_de2000\": " << report.maxDeltaE
			<< ",\n  \"p95_de2000\": " << report.p95DeltaE << ",\n  \"worst\": \"" << escape(report.worstLabel)
			<< "\",\n  \"pass\": " << (report.pass ? "true" : "false") << ",\n  \"pass_mean\": " << report.passMean
			<< ",\n  \"pass_max\": " << report.passMax << ",\n  \"patches\": [\n";
		for (size_t i = 0; i < report.entries.size(); ++i)
		{
			const VerifyEntry& e = report.entries[i];
			out << "    { \"label\": \"" << escape(e.label) << "\", \"kind\": \"" << ToString(e.kind)
				<< "\", \"de2000\": " << e.deltaE << ", \"target_xyz\": [" << e.targetXyz[0] << ", " << e.targetXyz[1]
				<< ", " << e.targetXyz[2] << "], \"measured_xyz\": [" << e.measuredXyz[0] << ", " << e.measuredXyz[1]
				<< ", " << e.measuredXyz[2] << "] }" << (i + 1 < report.entries.size() ? "," : "") << "\n";
		}
		out << "  ],\n  \"greyscale\": [\n";
		for (size_t i = 0; i < report.greys.size(); ++i)
		{
			const GreyStep& g = report.greys[i];
			out << "    { \"signal\": " << g.signal << ", \"luminance\": " << g.luminance << ", \"gamma\": " << g.gamma
				<< ", \"cct\": " << g.cct << " }" << (i + 1 < report.greys.size() ? "," : "") << "\n";
		}
		out << "  ]\n}\n";
		return out.str();
	}
}
