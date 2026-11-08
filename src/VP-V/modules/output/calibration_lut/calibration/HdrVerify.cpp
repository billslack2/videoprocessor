// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "HdrVerify.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Calibration
{
	namespace
	{
		const Mat3& Bt2020ToXyz()
		{
			static const Mat3 m = RgbToXyz(Gamuts::Bt2020());
			return m;
		}

		const Mat3& XyzToBt2020()
		{
			static const Mat3 m = Bt2020ToXyz().Inverse();
			return m;
		}
	}

	Vec3 XyzToIctcp(const Vec3& xyz)
	{
		const Vec3 rgb = XyzToBt2020() * xyz;
		const double L = (1688.0 * rgb[0] + 2146.0 * rgb[1] + 262.0 * rgb[2]) / 4096.0;
		const double M = (683.0 * rgb[0] + 2951.0 * rgb[1] + 462.0 * rgb[2]) / 4096.0;
		const double S = (99.0 * rgb[0] + 309.0 * rgb[1] + 3688.0 * rgb[2]) / 4096.0;
		const double l = PqEncode(L), m = PqEncode(M), s = PqEncode(S);
		return { 0.5 * l + 0.5 * m, (6610.0 * l - 13613.0 * m + 7003.0 * s) / 4096.0,
			(17933.0 * l - 17390.0 * m - 543.0 * s) / 4096.0 };
	}

	double DeltaEItp(const Vec3& a, const Vec3& b)
	{
		const Vec3 p = XyzToIctcp(a), q = XyzToIctcp(b);
		const double dI = p[0] - q[0], dT = 0.5 * (p[1] - q[1]), dP = p[2] - q[2];
		return 720.0 * std::sqrt(dI * dI + dT * dT + dP * dP);
	}

	double Bt2390Eetf(double nits, double lb, double lw, double tb, double tw)
	{
		const double pb = PqEncode(lb), pw = PqEncode(lw);
		const double span = pw - pb;
		if (!(span > 0.0)) return nits;
		const double maxLum = (PqEncode(tw) - pb) / span;
		const double minLum = (PqEncode(tb) - pb) / span;
		if (maxLum >= 1.0 && minLum <= 0.0) return (std::min)(nits, lw);
		double e = (PqEncode(nits) - pb) / span;
		e = (std::min)((std::max)(e, 0.0), 1.0);
		const double ks = 1.5 * maxLum - 0.5;
		double e2 = e;
		if (maxLum < 1.0 && e >= ks && ks < 1.0)
		{
			const double t = (e - ks) / (1.0 - ks);
			const double t2 = t * t, t3 = t2 * t;
			e2 = (2 * t3 - 3 * t2 + 1) * ks + (t3 - 2 * t2 + t) * (1.0 - ks) + (-2 * t3 + 3 * t2) * maxLum;
		}
		if (minLum > 0.0) e2 = e2 + minLum * std::pow(1.0 - e2, 4.0);
		return PqDecode(e2 * span + pb);
	}

	double ToneCurve::Map(double nits) const
	{
		if (!(nits > 0.0)) return kind == Kind::Bt2390 ? Bt2390Eetf(0.0, sourceBlack, sourcePeak, targetBlack, targetPeak) : 0.0;
		switch (kind)
		{
		case Kind::Bt2390:
			return Bt2390Eetf(nits, sourceBlack, sourcePeak, targetBlack, targetPeak);
		case Kind::Samples:
		{
			const size_t n = (std::min)(sourceNits.size(), outputNits.size());
			if (n < 2) return nits;
			if (nits <= sourceNits[0]) return outputNits[0] * nits / sourceNits[0];
			if (nits >= sourceNits[n - 1]) return outputNits[n - 1];
			size_t i = 1;
			while (i < n && sourceNits[i] < nits) ++i;
			// Linear in the PQ domain, where tone curves (BT.2390's Hermite
			// shoulder, libplacebo's spline) are smooth: far closer than log-log
			// between the renderer's 40 samples.
			const double x0 = PqEncode(sourceNits[i - 1]), x1 = PqEncode(sourceNits[i]);
			const double y0 = PqEncode(outputNits[i - 1]), y1 = PqEncode(outputNits[i]);
			const double t = x1 > x0 ? (PqEncode(nits) - x0) / (x1 - x0) : 0.0;
			return PqDecode(y0 + t * (y1 - y0));
		}
		default:
			return nits;
		}
	}

	ToneCurve ChooseToneCurve(const PatternOutputInfo& o, const CalibrationSettings& s, double measuredPeak)
	{
		ToneCurve c;
		if (s.hdrVerifyToneCurve == "none")
		{
			c.kind = ToneCurve::Kind::Identity;
			c.description = "no tone mapping (the PQ code's light is the target)";
			return c;
		}
		if (s.hdrVerifyToneCurve == "vp" && o.toneCurveCount >= 2)
		{
			if (!o.toneMappingActive)
			{
				c.kind = ToneCurve::Kind::Identity;
				c.description = std::string("VP's tone mapping (") + o.toneFunction + "): pass-through, the display peak "
					"VP maps to is not below the source's";
				return c;
			}
			c.kind = ToneCurve::Kind::Samples;
			for (uint32_t i = 0; i < o.toneCurveCount && i < PatternOutputInfo::ToneCurveMax; ++i)
			{
				if (!c.sourceNits.empty() && o.toneCurveSourceNits[i] <= c.sourceNits.back()) continue;
				c.sourceNits.push_back(o.toneCurveSourceNits[i]);
				c.outputNits.push_back(o.toneCurveOutputNits[i]);
			}
			c.sourcePeak = o.sourcePeakNits;
			c.targetPeak = o.targetPeakNits;
			c.description = std::string("VP's tone mapping (libplacebo ") + o.toneFunction + ", source peak " +
				FixedText(o.sourcePeakNits, 0) + " cd/m2 to " + FixedText(o.targetPeakNits, 0) + " cd/m2)";
			return c;
		}
		c.kind = ToneCurve::Kind::Bt2390;
		c.sourcePeak = o.sourcePeakNits > 0.0f ? o.sourcePeakNits : s.hdrVerifySourcePeak;
		c.targetPeak = o.targetPeakNits > 0.0f ? o.targetPeakNits : (measuredPeak > 0.0 ? measuredPeak : c.sourcePeak);
		c.targetBlack = 0.0;
		c.description = "BT.2390 EETF model (source peak " + FixedText(c.sourcePeak, 0) + " cd/m2 to " +
			FixedText(c.targetPeak, 0) + " cd/m2)" + (s.hdrVerifyToneCurve == "vp" ?
				"; VP's own curve was not reported (no HDR frame rendered yet)" : "");
		return c;
	}

	std::vector<HdrPatch> BuildHdrPlan(double sourcePeak)
	{
		std::vector<HdrPatch> p;
		HdrPatch peak;
		peak.label = "Peak (PQ 10000)";
		peak.sourceRgbNits = { 10000.0, 10000.0, 10000.0 };
		peak.peakProbe = true;
		p.push_back(peak);
		const double peakNits = (std::min)((std::max)(sourcePeak, 100.0), 10000.0);
		for (const double y : { 0.0, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 25.0, 50.0, 100.0, 203.0, 300.0, 400.0,
			600.0, 800.0, 1000.0, 1500.0, 2000.0, 4000.0, 10000.0 })
		{
			if (y > peakNits) break;
			HdrPatch g;
			char label[48];
			std::snprintf(label, sizeof(label), y == 203.0 ? "Grey %g cd/m2 (reference white)" : "Grey %g cd/m2", y);
			g.label = label;
			g.sourceRgbNits = { y, y, y };
			p.push_back(g);
		}
		if (peakNits > 1000.0 && std::fabs(peakNits - 1500.0) > 1 && std::fabs(peakNits - 2000.0) > 1 &&
			std::fabs(peakNits - 4000.0) > 1 && std::fabs(peakNits - 10000.0) > 1)
		{
			HdrPatch g;
			g.label = "Grey " + FixedText(peakNits, 0) + " cd/m2 (source peak)";
			g.sourceRgbNits = { peakNits, peakNits, peakNits };
			p.push_back(g);
		}
		// P3-D65 primaries / secondaries inside the BT.2020 container.
		const Mat3 p3To2020 = XyzToBt2020() * RgbToXyz(Gamuts::P3D65());
		const struct { const char* name; Vec3 rgb; } colours[] = {
			{ "Red", { 1, 0, 0 } }, { "Green", { 0, 1, 0 } }, { "Blue", { 0, 0, 1 } },
			{ "Cyan", { 0, 1, 1 } }, { "Magenta", { 1, 0, 1 } }, { "Yellow", { 1, 1, 0 } } };
		for (const double level : { 100.0, 400.0 })
		{
			if (level > peakNits) break;
			for (const auto& c : colours)
			{
				Vec3 rgb = p3To2020 * c.rgb;
				const double top = (std::max)((std::max)(rgb[0], rgb[1]), rgb[2]);
				for (double& v : rgb) v = (std::max)(v, 0.0) / top * level;
				HdrPatch h;
				h.label = std::string("P3 ") + c.name + " (max channel " + FixedText(level, 0) + " cd/m2)";
				h.sourceRgbNits = rgb;
				p.push_back(h);
			}
		}
		return p;
	}

	void HdrTarget(const HdrPatch& patch, const ToneCurve& curve, const PatternOutputInfo& format, PatchCodes& codes,
		Vec3& targetXyz)
	{
		Vec3 signal;
		for (int k = 0; k < 3; ++k)
		{
			const double mapped = patch.peakProbe ? patch.sourceRgbNits[k] : curve.Map(patch.sourceRgbNits[k]);
			signal[k] = HdrSignalForNits(format, mapped);
		}
		codes = CodesForSignal(format, signal);
		// The light the exact codes ask for.
		Vec3 rgb;
		for (int k = 0; k < 3; ++k) rgb[k] = PqDecode((std::min)((std::max)(codes.device[k], 0.0), 1.0));
		targetXyz = Bt2020ToXyz() * rgb;
	}

	void SummarizeHdr(HdrReport& r)
	{
		std::vector<double> values;
		r.maxItp = 0.0;
		r.worst.clear();
		for (const HdrEntry& e : r.entries)
		{
			if (e.aboveDisplayPeak || e.informative) continue;
			values.push_back(e.deltaEItp);
			if (e.deltaEItp > r.maxItp) { r.maxItp = e.deltaEItp; r.worst = e.label; }
		}
		r.counted = static_cast<int>(values.size());
		if (values.empty()) { r.meanItp = 0.0; r.p95Itp = 0.0; r.pass = false; return; }
		double sum = 0.0;
		for (const double v : values) sum += v;
		r.meanItp = sum / values.size();
		std::sort(values.begin(), values.end());
		r.p95Itp = values[(std::min)(values.size() - 1, static_cast<size_t>(std::ceil(0.95 * values.size())) - 1)];
		r.pass = r.meanItp <= 2.0 && r.maxItp <= 4.0;
	}

	std::string FormatHdrJson(const HdrReport& r)
	{
		std::ostringstream out;
		out << "{\n  \"curve\": " << JsonText(r.curve) << ",\n  \"metadata\": " << JsonText(r.metadata)
			<< ",\n  \"measured_peak\": " << JsonNumber(r.measuredPeak, 6)
			<< ",\n  \"mean_de_itp\": " << JsonNumber(r.meanItp, 5) << ",\n  \"p95_de_itp\": " << JsonNumber(r.p95Itp, 5)
			<< ",\n  \"max_de_itp\": " << JsonNumber(r.maxItp, 5) << ",\n  \"worst\": " << JsonText(r.worst)
			<< ",\n  \"pass\": " << (r.pass ? "true" : "false") << ",\n  \"patches\": [\n";
		for (size_t i = 0; i < r.entries.size(); ++i)
		{
			const HdrEntry& e = r.entries[i];
			out << "    {\"label\":" << JsonText(e.label) << ",\"source_nits\":" << JsonNumber(e.sourceNits, 6)
				<< ",\"codes\":[" << e.codes.code[0] << "," << e.codes.code[1] << "," << e.codes.code[2] << "]"
				<< ",\"target_xyz\":[" << JsonNumber(e.targetXyz[0], 7) << "," << JsonNumber(e.targetXyz[1], 7) << ","
				<< JsonNumber(e.targetXyz[2], 7) << "],\"measured_xyz\":[" << JsonNumber(e.measuredXyz[0], 7) << ","
				<< JsonNumber(e.measuredXyz[1], 7) << "," << JsonNumber(e.measuredXyz[2], 7) << "],\"de_itp\":"
				<< JsonNumber(e.deltaEItp, 5) << ",\"tracking_percent\":" << JsonNumber(e.trackingPercent, 5)
				<< ",\"above_display_peak\":" << (e.aboveDisplayPeak ? "true" : "false") << "}"
				<< (i + 1 < r.entries.size() ? "," : "") << "\n";
		}
		out << "  ]\n}\n";
		return out.str();
	}

	std::string FormatHdrReview(const HdrReport& r)
	{
		std::ostringstream out;
		out << "HDR (PQ) verify. Target: " << r.curve << ".\nMetadata VP sends: " << r.metadata << ".\nDisplay peak (measured): "
			<< FixedText(r.measuredPeak, 1) << " cd/m2.\n";
		if (r.counted > 0)
			out << "dE ITP over " << r.counted << " patches: mean " << FixedText(r.meanItp, 2) << ", 95th percentile "
				<< FixedText(r.p95Itp, 2) << ", max " << FixedText(r.maxItp, 2) << " (" << r.worst << ") - "
				<< (r.pass ? "pass (mean <= 2, max <= 4)" : "outside the guide (mean <= 2, max <= 4)") << ".\n";
		out << "Per patch (target / measured cd/m2, tracking, dE ITP):";
		for (const HdrEntry& e : r.entries)
		{
			out << "\n  " << e.label << ": " << FixedText(e.targetXyz[1], e.targetXyz[1] < 1 ? 4 : 2) << " / "
				<< FixedText(e.measuredXyz[1], e.measuredXyz[1] < 1 ? 4 : 2);
			if (!e.informative)
				out << ", " << (e.trackingPercent >= 0 ? "+" : "") << FixedText(e.trackingPercent, 1) << "%, dE ITP "
					<< FixedText(e.deltaEItp, 2);
			if (e.aboveDisplayPeak) out << " (above the display's peak: not counted)";
			else if (e.informative) out << " (reported, not counted)";
		}
		return out.str();
	}

	std::string FormatHdrHtml(const HdrReport& r)
	{
		std::ostringstream out;
		out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>VP HDR verify</title><style>body{font-family:Segoe UI,"
			"Arial,sans-serif;margin:24px}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:3px 8px;"
			"text-align:right}th{background:#eee}</style></head><body><h1>HDR (PQ) verify</h1><p>Target: " << r.curve
			<< "<br>Metadata VP sends: " << r.metadata << "<br>Display peak (measured): " << FixedText(r.measuredPeak, 1)
			<< " cd/m&sup2;<br>dE ITP mean " << FixedText(r.meanItp, 2) << ", max " << FixedText(r.maxItp, 2) << " - "
			<< (r.pass ? "pass" : "outside the guide") << "</p><table><tr><th>Patch</th><th>Target Y</th><th>Measured Y</th>"
			"<th>Tracking %</th><th>dE ITP</th></tr>";
		for (const HdrEntry& e : r.entries)
			out << "<tr><td style=\"text-align:left\">" << e.label << (e.aboveDisplayPeak ? " (above peak)" : "") << "</td><td>"
				<< FixedText(e.targetXyz[1], 4) << "</td><td>" << FixedText(e.measuredXyz[1], 4) << "</td><td>"
				<< FixedText(e.trackingPercent, 2) << "</td><td>" << FixedText(e.deltaEItp, 2) << "</td></tr>";
		out << "</table></body></html>\n";
		return out.str();
	}

	HdrVerifyRun::HdrVerifyRun(const CalibrationSettings& s, const SessionEnvironment& e, std::unique_ptr<IMeter> m,
		double floor, IPatternSink& p, ISessionClock& c)
		: MeterRun("hdr_verify", s, e, std::move(m), floor, p, c)
	{
	}

	HdrVerifyRun::~HdrVerifyRun()
	{
		Shutdown();
	}

	bool HdrVerifyRun::OnPatternsStarted(std::string& error)
	{
		if (format.transfer != SignalTransfer::Pq)
		{
			error = "The HDR verify needs VP's output in HDR (PQ). Play an HDR source with VP's HDR output on, then run "
				"it again (VP's output is " + FormatName() + ").";
			return false;
		}
		return true;
	}

	void HdrVerifyRun::BeginWork()
	{
		const double sourcePeak = format.sourcePeakNits > 0.0f ? format.sourcePeakNits : settings.hdrVerifySourcePeak;
		plan = BuildHdrPlan(sourcePeak);
		index = 0;
		std::ostringstream metadata;
		if (format.sentMaxLuma > 0.0f || format.sentMaxCll > 0.0f)
			metadata << "mastering max " << FixedText(format.sentMaxLuma, 0) << " / min " << FixedText(format.sentMinLuma, 4)
				<< " cd/m2, MaxCLL " << FixedText(format.sentMaxCll, 0) << ", MaxFALL " << FixedText(format.sentMaxFall, 0);
		else
			metadata << "not reported by the renderer";
		report.metadata = metadata.str();
		SetProgress(0, static_cast<int>(plan.size()));
		SetStep(SessionStep::Measuring, "Measuring PQ patches. Keep the room dark and the meter still.");
	}

	void HdrVerifyRun::StepWork(SessionStep step, Command)
	{
		if (step != SessionStep::Measuring) { clock.Sleep(0.05); return; }
		if (index >= plan.size())
		{
			EndPatterns();
			SummarizeHdr(report);
			std::string error;
			WriteTextFile(folder.runDir + "\\hdr_verify.json", FormatHdrJson(report), error);
			WriteTextFile(folder.reportHtml, FormatHdrHtml(report), error);
			const std::string review = FormatHdrReview(report);
			{
				std::lock_guard<std::mutex> g(mutex);
				status.review = review;
				status.verifyMean = report.meanItp;
				status.verifyMax = report.maxItp;
				status.verifyPass = report.pass;
				status.diff.clear();
			}
			WriteIndex("verified");
			Finish(SessionStep::Done, "HDR verify finished: dE ITP mean " + FixedText(report.meanItp, 2) + ", max " +
				FixedText(report.maxItp, 2) + ".");
			return;
		}
		const HdrPatch& patch = plan[index];
		if (index == 1)
		{
			// After the peak probe: the BT.2390 fallback may use the measured peak.
			curve = ChooseToneCurve(format, settings, report.measuredPeak);
			report.curve = curve.description;
		}
		HdrEntry entry;
		entry.label = patch.label;
		entry.sourceNits = (Bt2020ToXyz() * patch.sourceRgbNits)[1];
		entry.grey = patch.sourceRgbNits[0] == patch.sourceRgbNits[1] && patch.sourceRgbNits[1] == patch.sourceRgbNits[2];
		entry.informative = patch.peakProbe || !(entry.sourceNits > 0.0);
		HdrTarget(patch, curve, format, entry.codes, entry.targetXyz);
		std::string error;
		if (!ShowPatch(entry.codes, entry.targetXyz[1], error)) { Fail(error); return; }
		double elapsed = 0.0;
		int reads = 0;
		bool floorLimited = false;
		std::string note;
		const ReadOutcome o = MeasureCodes(elapsed, entry.measuredXyz, reads, floorLimited, note);
		if (o == ReadOutcome::Stop) return;
		if (o == ReadOutcome::Ok)
		{
			if (patch.peakProbe)
			{
				report.measuredPeak = entry.measuredXyz[1];
				whiteY = report.measuredPeak;
			}
			entry.aboveDisplayPeak = !patch.peakProbe && report.measuredPeak > 0.0 &&
				entry.targetXyz[1] > report.measuredPeak * 1.02;
			entry.deltaEItp = DeltaEItp(entry.targetXyz, entry.measuredXyz);
			entry.trackingPercent = entry.targetXyz[1] > 0.0 ? (entry.measuredXyz[1] / entry.targetXyz[1] - 1.0) * 100.0 : 0.0;
			report.entries.push_back(entry);
		}
		WriteRaw("{\"phase\":\"hdr\",\"label\":" + JsonText(entry.label) + ",\"codes\":[" + std::to_string(entry.codes.code[0]) +
			"," + std::to_string(entry.codes.code[1]) + "," + std::to_string(entry.codes.code[2]) + "],\"target_y\":" +
			JsonNumber(entry.targetXyz[1], 7) + ",\"xyz\":[" + JsonNumber(entry.measuredXyz[0], 8) + "," +
			JsonNumber(entry.measuredXyz[1], 8) + "," + JsonNumber(entry.measuredXyz[2], 8) + "],\"status\":" +
			(o == ReadOutcome::Ok ? "\"ok\"" : "\"unreadable\"") + (note.empty() ? std::string() : ",\"note\":" + JsonText(note)) + "}");
		{
			std::lock_guard<std::mutex> g(mutex);
			++status.measured;
			if (floorLimited) ++status.floorLimited;
		}
		++index;
		SetProgress(static_cast<int>(index), static_cast<int>(plan.size()));
	}
}
