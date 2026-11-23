#include "StrangePlants.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "ProcMeshBuild.h"

using namespace UE::Geometry;

namespace SlonkStrange
{
	using SlonkProcMesh::FBuild;
	using SlonkProcMesh::SolidSurface;

	/** The height a plant is laid out to before it is fitted, cm. */
	constexpr double TopCm = 100.0;
	/** The height the tallest point of a fitted plant stands at, and the widths it is kept between, cm. */
	constexpr double FitTopCm = 99.0;
	constexpr double NarrowestCm = 30.0, WidestCm = 150.0;
	/** A giant sways this share of what a tree does: the wind moves a tip by a share of the plant's height, and a
	 *  giant's is two hundred metres. */
	constexpr float GiantSway = 0.25f;

	int32 ClassRow(EFloraClass C) { return FMath::Clamp((int32)C, 0, 3); }
	bool TreeLike(EFloraClass C) { return C == EFloraClass::Tree || C == EFloraClass::Giant; }

	/** How far a point sways, by its height: nothing at the foot, most at the top, and stiffer than a leafy crown. */
	float SwayAt(double Z) { return (float)(0.8 * FMath::Pow(FMath::Clamp(Z / TopCm, 0.0, 1.0), 1.3)); }

	/** A triangle wound to show the side its corners' normals point to, as every part of a plant is. */
	void TriN(FBuild& B, int32 A, int32 Bv, int32 C)
	{
		const FVector Pa = (FVector)B.Mesh.GetVertex(A), Pb = (FVector)B.Mesh.GetVertex(Bv), Pc = (FVector)B.Mesh.GetVertex(C);
		// The mesh's own reading of a winding (the engine's left-handed frame).
		const FVector Geo = FVector::CrossProduct(Pc - Pa, Pb - Pa);
		const FVector Nrm = (FVector)(B.N->GetElement(A) + B.N->GetElement(Bv) + B.N->GetElement(C));
		if (FVector::DotProduct(Geo, Nrm) >= 0.0) { B.Tri(A, Bv, C); }
		else { B.Tri(A, C, Bv); }
	}
	void QuadN(FBuild& B, int32 A, int32 Bv, int32 C, int32 D) { TriN(B, A, Bv, C); TriN(B, A, C, D); }

	/** Which way a turned surface looks: out from its axis (a bladder's wall), up (a cap's top) or down (its underside). */
	enum class EFace : uint8 { Out, Up, Down };
	/** A point of a turned surface's profile: its distance from the axis, its height over the centre, and its UV's y. */
	struct FTurn { double R; double Z; float V; };

	/**
	 * A surface turned about the vertical through Centre: a ring of Sides at each point of the profile (one vertex where
	 * the profile meets the axis), its normals across the profile's slope on the side Face says, its UV x once round and
	 * y the profile's own, its colour foliage's (or a stem's, by Part).
	 */
	void Lathe(FBuild& B, const FVector& Centre, TConstArrayView<FTurn> Profile, int32 Sides, EFace Face, float Part, float Rand, double Turn0)
	{
		TArray<TArray<int32>> Rings;
		const int32 Num = Profile.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const FTurn& P = Profile[i];
			const FTurn& P0 = Profile[FMath::Max(i - 1, 0)];
			const FTurn& P1 = Profile[FMath::Min(i + 1, Num - 1)];
			const double Dr = P1.R - P0.R, Dz = P1.Z - P0.Z;
			double Nr = Dz, Nz = -Dr;
			if ((Face == EFace::Out && Nr < 0.0) || (Face == EFace::Up && Nz < 0.0) || (Face == EFace::Down && Nz > 0.0)) { Nr = -Nr; Nz = -Nz; }
			const FVector At = Centre + FVector(0.0, 0.0, P.Z);
			const FVector4f Col(Part, SwayAt(At.Z), Rand, SolidSurface);
			TArray<int32> Ring;
			if (P.R <= 1e-4)
			{
				const double Sign = Face == EFace::Down ? -1.0 : (Face == EFace::Up ? 1.0 : (i == 0 ? -1.0 : 1.0));
				Ring.Add(B.Vert(At, FVector(0.0, 0.0, Sign), FVector2f(0.5f, P.V), Col));
			}
			else
			{
				for (int32 s = 0; s <= Sides; ++s)
				{
					const double A = Turn0 + UE_DOUBLE_TWO_PI * s / Sides;
					const FVector D(FMath::Cos(A), FMath::Sin(A), 0.0);
					Ring.Add(B.Vert(At + D * P.R, D * Nr + FVector(0.0, 0.0, Nz), FVector2f((float)s / Sides, P.V), Col));
				}
			}
			Rings.Add(MoveTemp(Ring));
		}
		for (int32 i = 0; i + 1 < Rings.Num(); ++i)
		{
			const TArray<int32>& Lo = Rings[i];
			const TArray<int32>& Hi = Rings[i + 1];
			if (Lo.Num() == 1 && Hi.Num() == 1) { continue; }
			for (int32 s = 0; s < Sides; ++s)
			{
				if (Lo.Num() == 1) { TriN(B, Lo[0], Hi[s], Hi[s + 1]); }
				else if (Hi.Num() == 1) { TriN(B, Lo[s], Lo[s + 1], Hi[0]); }
				else { QuadN(B, Lo[s], Lo[s + 1], Hi[s + 1], Hi[s]); }
			}
		}
	}

	/** What a plant laid out tells of itself, for its trunk's capsule and its far crown. */
	struct FRaw
	{
		/** Its stalk's radius at the foot and the height its bare part reaches, as laid out. */
		double TrunkR = 0.0, TrunkTop = 0.0;
		/** Where its top is widest, as a share of its foliage's height: its far crown's waist. */
		double WaistAt = 0.5;
		/** Its far crown runs down under its foliage to a point, as a cap's gills run in to its stalk. */
		bool bConeUnder = false;
	};

	// ---- A stalk under a cap ----

	/** Sides round a cap, rings across its top, its underside (none, a fan, a fan and a band), its stalk's sides and
	 *  rings, and the most caps drawn: by class, then by level of detail. */
	struct FCapDetail { int32 Sides, Rings, Under, StalkSides, StalkPts, MaxCaps; };
	constexpr FCapDetail CapDetail[4][3] = {
		{ { 5, 1, 1, 3, 2, 5 }, { 4, 1, 0, 3, 2, 3 }, { 3, 1, 0, 0, 0, 2 } },
		{ { 9, 2, 1, 4, 3, 5 }, { 6, 1, 1, 3, 2, 5 }, { 4, 1, 0, 0, 0, 5 } },
		{ { 14, 3, 2, 6, 4, 4 }, { 9, 2, 1, 4, 3, 4 }, { 6, 1, 1, 3, 2, 4 } },
		{ { 24, 4, 2, 10, 4, 4 }, { 14, 3, 1, 6, 3, 4 }, { 8, 2, 1, 4, 2, 4 } } };

	/** A cap about its rim's centre: its top from its middle (Hc over the rim: a dome's above it, a funnel's below) out
	 *  to the rim, and its underside from the stalk out to the same rim, thicker at the middle. */
	void Cap(FBuild& B, const FVector& RimCentre, double R, double Hc, const FCapDetail& D, float Rand, double Turn0)
	{
		const double Thick = 0.10 * R + 0.25 * FMath::Abs(Hc);
		TArray<FTurn> Over;
		for (int32 k = 0; k <= D.Rings; ++k)
		{
			const double T = (double)k / D.Rings;
			Over.Add({ R * T, Hc * (1.0 - T * T), (float)T });
		}
		Lathe(B, RimCentre, Over, D.Sides, EFace::Up, 1.f, Rand, Turn0);
		if (D.Under > 0)
		{
			TArray<FTurn> Under;
			Under.Add({ 0.0, Hc - Thick, 0.f });
			if (D.Under > 1) { Under.Add({ R * 0.55, (Hc - Thick) * (1.0 - 0.55 * 0.55), 0.55f }); }
			Under.Add({ R, 0.0, 1.f });
			Lathe(B, RimCentre, Under, D.Sides, EFace::Down, 1.f, Rand, Turn0);
		}
	}

	void BuildStalkCap(const FFloraForm& F, int32 Lod, FBuild& B, FRaw& Raw)
	{
		const FCapDetail& D = CapDetail[ClassRow(F.Class)][FMath::Clamp(Lod, 0, 2)];
		FRandomStream Rng(4409 + 7919 * (int32)F.Variant);
		// A cap's middle over its rim: a dome's high over it, a parasol's a little, a funnel's below it, open to the sky.
		auto MiddleOver = [&F](double R) { return F.Arch < 0.5f ? FMath::Lerp(0.6, 0.1, (double)F.Arch * 2.0) * R : FMath::Lerp(0.1, -0.5, ((double)F.Arch - 0.5) * 2.0) * R; };
		const double LeanA = Rng.FRandRange(0.f, UE_TWO_PI);
		const FVector LeanDir(FMath::Cos(LeanA), FMath::Sin(LeanA), 0.0);
		if (TreeLike(F.Class))
		{
			// One stalk, a cap at its top and up to three more down it, each a little smaller.
			const int32 Tiers = FMath::Clamp(F.Parts == 0 ? 1 : (int32)F.Parts, 1, 4);
			const double R = FMath::Lerp(18.0, 58.0, (double)F.Spread);
			const double Hc = MiddleOver(R);
			const double RimZ = TopCm - FMath::Max(Hc, 0.0);
			const double Lowest = FMath::Lerp(38.0, 70.0, (double)F.Stem);
			const double StalkTop = RimZ + Hc - 0.1 * R;
			const FVector TopOff = LeanDir * ((double)F.Lean * 20.0);
			auto AxisAt = [&](double Z) { const double T = FMath::Clamp(Z / RimZ, 0.0, 1.0); return TopOff * (T * T) + FVector(0.0, 0.0, Z); };
			const double R0 = 2.2 + 0.055 * R;
			if (D.StalkSides >= 3)
			{
				TArray<FVector> Path;
				TArray<double> Radii;
				for (int32 p = 0; p < D.StalkPts; ++p)
				{
					const double T = (double)p / (D.StalkPts - 1);
					Path.Add(AxisAt(FMath::Lerp(-4.0, StalkTop, T)));
					Radii.Add(R0 * FMath::Lerp(1.0, 0.55, T));
				}
				B.Tube(Path, Radii, D.StalkSides, 0.0, SwayAt(StalkTop), 0.3f);
			}
			for (int32 i = 0; i < Tiers; ++i)
			{
				const double Rz = Tiers == 1 ? RimZ : FMath::Lerp(RimZ, Lowest, (double)i / (Tiers - 1));
				const double Ri = R * (1.0 - 0.17 * i);
				const float Rand = Rng.FRand();
				const double Turn = Rng.FRandRange(0.f, UE_TWO_PI);
				FVector C = AxisAt(Rz);
				C.Z = Rz;
				Cap(B, C, Ri, MiddleOver(Ri), D, Rand, Turn);
			}
			Raw.TrunkR = R0;
			Raw.TrunkTop = Tiers == 1 ? StalkTop : Lowest - 0.3 * R;
			Raw.WaistAt = Hc >= 0.0 ? 0.2 : 0.85;
			Raw.bConeUnder = Tiers == 1;
			return;
		}
		// A clump: a tall one in the middle and smaller ones round it, each on a stalk of its own.
		const bool bShrub = F.Class == EFloraClass::Shrub;
		const int32 Caps = FMath::Clamp(F.Parts == 0 ? 4 : 2 + (int32)F.Parts, 3, 5);
		const double Ring = bShrub ? FMath::Lerp(12.0, 30.0, (double)F.Spread) : FMath::Lerp(8.0, 20.0, (double)F.Spread);
		const double CapR = bShrub ? FMath::Lerp(9.0, 22.0, (double)F.Spread) : FMath::Lerp(5.0, 12.0, (double)F.Spread);
		for (int32 i = 0; i < Caps; ++i)
		{
			const double A = LeanA + UE_DOUBLE_TWO_PI * (i + Rng.FRandRange(-0.25f, 0.25f)) / FMath::Max(Caps - 1, 1);
			const double Out = i == 0 ? 0.0 : Ring * Rng.FRandRange(0.7f, 1.f);
			const double High = i == 0 ? TopCm : TopCm * Rng.FRandRange(0.45f, 0.85f);
			const double Ri = CapR * (i == 0 ? 1.0 : (double)Rng.FRandRange(0.6f, 0.95f));
			const float Rand = Rng.FRand();
			const double Turn = Rng.FRandRange(0.f, UE_TWO_PI);
			if (i >= D.MaxCaps) { continue; }
			const double Hc = MiddleOver(Ri);
			const double RimZ = High - FMath::Max(Hc, 0.0);
			const FVector O(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Foot = O * (Out * 0.35) + FVector(0.0, 0.0, -1.0);
			const FVector Head = O * Out + FVector(0.0, 0.0, RimZ);
			if (D.StalkSides >= 3)
			{
				const double Sr = 0.9 + 0.09 * Ri;
				const FVector Under = Head + FVector(0.0, 0.0, Hc - 0.1 * Ri);
				TArray<FVector> Path;
				TArray<double> Radii;
				for (int32 p = 0; p < D.StalkPts; ++p)
				{
					const double T = (double)p / (D.StalkPts - 1);
					Path.Add(FMath::Lerp(Foot, Under, T));
					Radii.Add(Sr * FMath::Lerp(1.0, 0.6, T));
				}
				B.Tube(Path, Radii, D.StalkSides, 0.0, SwayAt(RimZ), Rand);
			}
			Cap(B, Head, Ri, Hc, D, Rand, Turn);
		}
	}

	// ---- Fans ----

	/** Strips across a fan and rows along it, its stem's sides and rings, and the most fans drawn. */
	struct FFanDetail { int32 Across, Along, StalkSides, StalkPts, MaxFans; };
	constexpr FFanDetail FanDetail[4][3] = {
		{ { 4, 3, 0, 0, 4 }, { 2, 2, 0, 0, 4 }, { 2, 1, 0, 0, 3 } },
		{ { 6, 4, 0, 0, 5 }, { 4, 2, 0, 0, 5 }, { 2, 1, 0, 0, 5 } },
		{ { 10, 5, 6, 3, 6 }, { 6, 3, 4, 3, 6 }, { 2, 2, 3, 2, 6 } },
		{ { 14, 7, 10, 4, 6 }, { 8, 4, 6, 3, 6 }, { 4, 2, 4, 2, 6 } } };

	/** How far out and how far up a fan's middle line has run at V of its length, for a fan of unit length leaning out
	 *  by Tilt at its root and by Tilt + Bend at its rim (an arc). */
	void FanRun(double Tilt, double Bend, double V, double& OutRun, double& UpRun)
	{
		if (FMath::Abs(Bend) < 1e-3) { OutRun = FMath::Sin(Tilt) * V; UpRun = FMath::Cos(Tilt) * V; return; }
		const double T = Tilt + Bend * V;
		OutRun = (FMath::Cos(Tilt) - FMath::Cos(T)) / Bend;
		UpRun = (FMath::Sin(T) - FMath::Sin(Tilt)) / Bend;
	}

	/** One fan: a sheet spread from its root through HalfAngle either way, leaning out along Out and bending over
	 *  toward its rim, its edges curled a little toward the sky; UV x across it, y from its root to its rim. */
	void Sheet(FBuild& B, const FVector& Root, const FVector& Out, double Tilt, double Bend, double Len, double HalfAngle, int32 Across, int32 Along, float Rand)
	{
		const FVector Side(-Out.Y, Out.X, 0.0);
		TArray<int32> Prev;
		for (int32 j = 0; j <= Along; ++j)
		{
			const double V = (double)j / Along;
			const double T = Tilt + Bend * V;
			// The face that looks up and back in toward the stem: the side the sky lights.
			const FVector Sky = FVector::ZAxisVector * FMath::Sin(T) - Out * FMath::Cos(T);
			TArray<int32> Row;
			if (j == 0)
			{
				Row.Add(B.Vert(Root, Sky * 0.7 + FVector::ZAxisVector * 0.3, FVector2f(0.5f, 0.f), FVector4f(1.f, SwayAt(Root.Z), Rand, SolidSurface)));
			}
			else
			{
				double OutRun, UpRun;
				FanRun(Tilt, Bend, V, OutRun, UpRun);
				const FVector Mid = (Out * OutRun + FVector::ZAxisVector * UpRun) * Len;
				for (int32 i = 0; i <= Across; ++i)
				{
					const double U = (double)i / Across, Al = (U - 0.5) * 2.0 * HalfAngle;
					const FVector P = Root + Mid * FMath::Cos(Al) + Side * (FMath::Sin(Al) * Len * V) + Sky * (0.12 * Len * V * FMath::Square(FMath::Sin(Al)));
					const FVector Nrm = Sky * 0.7 + FVector::ZAxisVector * 0.3 - Side * (0.25 * FMath::Sin(Al));
					Row.Add(B.Vert(P, Nrm, FVector2f((float)U, (float)V), FVector4f(1.f, SwayAt(P.Z), Rand, SolidSurface)));
				}
			}
			if (j == 1) { for (int32 i = 0; i < Across; ++i) { TriN(B, Prev[0], Row[i], Row[i + 1]); } }
			else if (j > 1) { for (int32 i = 0; i < Across; ++i) { QuadN(B, Prev[i], Prev[i + 1], Row[i + 1], Row[i]); } }
			Prev = MoveTemp(Row);
		}
	}

	void BuildFan(const FFloraForm& F, int32 Lod, FBuild& B, FRaw& Raw)
	{
		const FFanDetail& D = FanDetail[ClassRow(F.Class)][FMath::Clamp(Lod, 0, 2)];
		FRandomStream Rng(5507 + 7919 * (int32)F.Variant);
		const bool bTrunk = TreeLike(F.Class);
		const int32 Most = bTrunk ? 6 : (F.Class == EFloraClass::Shrub ? 5 : 4);
		const int32 Fans = FMath::Clamp(F.Parts == 0 ? (bTrunk ? 4 : 3) : 2 + (int32)F.Parts, 2, Most);
		// A tree's fans lean far out and bend over, a palm's; a tuft's and a bush's stand nearer the upright, or they
		// would lie wider than they are tall.
		const double Tilt = FMath::DegreesToRadians(bTrunk ? FMath::Lerp(14.0, 55.0, (double)F.Arch) : FMath::Lerp(8.0, 26.0, (double)F.Arch));
		const double Bend = bTrunk ? FMath::Lerp(0.05, 0.6, (double)F.Arch) : FMath::Lerp(0.0, 0.2, (double)F.Arch);
		const double HalfAngle = FMath::DegreesToRadians(bTrunk ? FMath::Lerp(24.0, 62.0, (double)F.Spread) : FMath::Lerp(18.0, 38.0, (double)F.Spread));
		double OutRun, UpRun;
		FanRun(Tilt, Bend, 1.0, OutRun, UpRun);
		// How far from the stem a unit fan's rim reaches, at its corners.
		const double Reach = FMath::Sqrt(FMath::Square(OutRun * FMath::Cos(HalfAngle)) + FMath::Square(FMath::Sin(HalfAngle)));
		double Len = TopCm / FMath::Max(UpRun, 0.2), StemTop = 0.0;
		if (bTrunk)
		{
			// As long as its reach allows, on a trunk tall enough to carry its rim to the top: the more a fan droops,
			// the taller its trunk.
			const double LeastStem = FMath::Lerp(16.0, 50.0, (double)F.Stem);
			Len = FMath::Min((TopCm - LeastStem) / FMath::Max(UpRun, 0.2), 62.0 / FMath::Max(Reach, 0.2));
			StemTop = TopCm - UpRun * Len;
		}
		const double LeanA = Rng.FRandRange(0.f, UE_TWO_PI);
		const FVector TopOff = FVector(FMath::Cos(LeanA), FMath::Sin(LeanA), 0.0) * (bTrunk ? (double)F.Lean * 18.0 : 0.0);
		const FVector Crown = TopOff + FVector(0.0, 0.0, bTrunk ? StemTop : -1.0);
		const double R0 = bTrunk ? FMath::Lerp(2.6, 4.6, (double)F.Spread) : 0.0;
		if (bTrunk && D.StalkSides >= 3)
		{
			TArray<FVector> Path;
			TArray<double> Radii;
			for (int32 p = 0; p < D.StalkPts; ++p)
			{
				const double T = (double)p / (D.StalkPts - 1);
				Path.Add(TopOff * (T * T) + FVector(0.0, 0.0, FMath::Lerp(-4.0, StemTop, T)));
				Radii.Add(R0 * FMath::Lerp(1.0, 0.6, T));
			}
			B.Tube(Path, Radii, D.StalkSides, 0.0, SwayAt(StemTop), 0.3f);
		}
		const double Turn = Rng.FRandRange(0.f, UE_TWO_PI);
		for (int32 i = 0; i < Fans; ++i)
		{
			const double A = Turn + UE_DOUBLE_TWO_PI * (i + Rng.FRandRange(-0.18f, 0.18f)) / Fans;
			const double LenI = Len * (i == 0 ? 1.0 : (double)Rng.FRandRange(0.78f, 1.f));
			const double TiltI = Tilt * (i == 0 ? 1.0 : (double)Rng.FRandRange(0.85f, 1.15f));
			const float Rand = Rng.FRand();
			if (i >= D.MaxFans) { continue; }
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			Sheet(B, Crown + Out * (R0 * 0.4), Out, TiltI, Bend, LenI, HalfAngle, D.Across, D.Along, Rand);
		}
		Raw.TrunkR = R0;
		Raw.TrunkTop = StemTop;
		Raw.WaistAt = 0.6;
	}

	// ---- A cluster of tubes ----

	/** Sides round a tube (0: a single blade stands for it), the rings along it, whether its mouth has a floor, and the
	 *  most tubes drawn. */
	struct FTubeDetail { int32 Sides, Pts, Throat, MaxTubes; };
	constexpr FTubeDetail TubeDetail[4][3] = {
		{ { 4, 3, 0, 5 }, { 3, 2, 0, 5 }, { 0, 0, 0, 5 } },
		{ { 6, 4, 1, 6 }, { 4, 3, 0, 6 }, { 3, 2, 0, 4 } },
		{ { 8, 5, 1, 8 }, { 5, 3, 0, 8 }, { 3, 2, 0, 8 } },
		{ { 14, 6, 1, 9 }, { 8, 4, 0, 9 }, { 4, 2, 0, 9 } } };
	/** Where along a tube its rings stand, for each count of them: the last two shape its lip. */
	constexpr double TubeRings[7][6] = { { 0 }, { 0 }, { 0.0, 1.0 }, { 0.0, 0.93, 1.0 }, { 0.0, 0.5, 0.93, 1.0 }, { 0.0, 0.33, 0.66, 0.93, 1.0 }, { 0.0, 0.25, 0.5, 0.75, 0.93, 1.0 } };

	void BuildTubes(const FFloraForm& F, int32 Lod, FBuild& B, FRaw& Raw)
	{
		const FTubeDetail& D = TubeDetail[ClassRow(F.Class)][FMath::Clamp(Lod, 0, 2)];
		FRandomStream Rng(6607 + 7919 * (int32)F.Variant);
		const bool bTrunk = TreeLike(F.Class);
		const bool bShrub = F.Class == EFloraClass::Shrub;
		const int32 Most = F.Class == EFloraClass::Giant ? 9 : (bTrunk ? 8 : (bShrub ? 6 : 5));
		const int32 Tubes = FMath::Clamp(F.Parts == 0 ? (bTrunk ? 6 : (bShrub ? 5 : 4)) : 1 + 2 * (int32)F.Parts, 3, Most);
		const double TubeR = bTrunk ? FMath::Lerp(3.2, 8.0, (double)F.Spread) : (bShrub ? FMath::Lerp(5.0, 11.0, (double)F.Spread) : FMath::Lerp(3.0, 7.0, (double)F.Spread));
		const double Splay = FMath::DegreesToRadians(3.0 + 24.0 * (double)F.Lean);
		const double Flare = FMath::Lerp(1.05, 1.7, (double)F.Arch);
		const double FootR = TubeR * (0.6 + 0.45 * FMath::Sqrt((double)Tubes));
		const double Turn = Rng.FRandRange(0.f, UE_TWO_PI);
		const double FootZ = bTrunk ? -4.0 : -1.0;
		for (int32 i = 0; i < Tubes; ++i)
		{
			const double A = Turn + UE_DOUBLE_TWO_PI * (i + Rng.FRandRange(-0.2f, 0.2f)) / FMath::Max(Tubes - 1, 1);
			const double Dist = i == 0 ? 0.0 : FootR * Rng.FRandRange(0.6f, 1.f);
			const double High = i == 0 ? TopCm : TopCm * Rng.FRandRange(0.42f, 0.9f);
			const double Ri = TubeR * Rng.FRandRange(0.75f, 1.1f) * (0.8 + 0.2 * High / TopCm);
			const float Rand = Rng.FRand();
			if (i >= D.MaxTubes) { continue; }
			const FVector O(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Side(-O.Y, O.X, 0.0);
			const FVector Foot = O * Dist + FVector(0.0, 0.0, FootZ);
			// Splayed out from the upright, the more the further from the middle it stands.
			const double Ang = Splay * (i == 0 ? 0.0 : Dist / FootR);
			const FVector Dir = O * FMath::Sin(Ang) + FVector::ZAxisVector * FMath::Cos(Ang);
			const FVector Lip = Foot + Dir * ((High - FootZ) / FMath::Cos(Ang));
			if (D.Sides < 3)
			{
				// A tuft a few pixels tall: each tube a blade, lit as its outer wall is.
				const FVector Nrm = O * 0.6 + FVector::ZAxisVector * 0.4 + (i == 0 ? FVector(0.3, 0.0, 0.0) : FVector::ZeroVector);
				const int32 V0 = B.Vert(Foot - Side * Ri, Nrm, FVector2f(0.f, 0.f), FVector4f(1.f, 0.f, Rand, SolidSurface));
				const int32 V1 = B.Vert(Foot + Side * Ri, Nrm, FVector2f(1.f, 0.f), FVector4f(1.f, 0.f, Rand, SolidSurface));
				const int32 V2 = B.Vert(Lip, Nrm, FVector2f(0.5f, 1.f), FVector4f(1.f, SwayAt(High), Rand, SolidSurface));
				TriN(B, V0, V1, V2);
				continue;
			}
			TArray<FVector> Path;
			TArray<double> Radii;
			for (int32 p = 0; p < D.Pts; ++p)
			{
				const double T = TubeRings[D.Pts][p];
				// Bowed a little outward along its length, narrowing to its neck and opening at its lip.
				Path.Add(FMath::Lerp(Foot, Lip, T) + O * (0.03 * High * FMath::Sin(UE_DOUBLE_PI * T) * (i == 0 ? 0.0 : 1.0)));
				Radii.Add(D.Pts > 2 && p == D.Pts - 1 ? Ri * Flare : (T >= 0.9 ? Ri * 0.96 : Ri * FMath::Lerp(1.08, 0.92, T / 0.9)));
			}
			B.Tube(Path, Radii, D.Sides, 0.0, SwayAt(High), Rand, 1.f);
			if (D.Throat > 0)
			{
				// A floor a little way down its mouth, so the pipe is not seen through to the ground.
				const FVector At = FMath::Lerp(Foot, Lip, 0.88);
				const FVector4f Col(1.f, SwayAt(At.Z), Rand * 0.25f, SolidSurface);
				const FVector X = FVector::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector).GetSafeNormal();
				const FVector Y = FVector::CrossProduct(Dir, X);
				const int32 Mid = B.Vert(At, Dir, FVector2f(0.5f, 0.4f), Col);
				TArray<int32> Rim;
				for (int32 s = 0; s <= D.Sides; ++s)
				{
					const double Sa = UE_DOUBLE_TWO_PI * s / D.Sides;
					Rim.Add(B.Vert(At + (X * FMath::Cos(Sa) + Y * FMath::Sin(Sa)) * (Ri * 0.94), Dir, FVector2f(0.5f, 0.4f), Col));
				}
				for (int32 s = 0; s < D.Sides; ++s) { TriN(B, Mid, Rim[s], Rim[s + 1]); }
			}
		}
		Raw.TrunkR = TubeR;
		Raw.TrunkTop = 70.0;
		Raw.WaistAt = 0.7;
	}

	// ---- Bladders on stalks ----

	/** Sides round a bladder, the rings up it (0: two pyramids base to base stand for it), its stalk's sides and rings,
	 *  and the most bladders drawn. */
	struct FBulbDetail { int32 Sides, Rings, StalkSides, StalkPts, MaxBulbs; };
	constexpr FBulbDetail BulbDetail[4][3] = {
		{ { 4, 3, 3, 2, 3 }, { 3, 0, 0, 0, 3 }, { 3, 0, 0, 0, 1 } },
		{ { 7, 4, 3, 2, 5 }, { 5, 2, 3, 2, 5 }, { 3, 0, 0, 0, 4 } },
		{ { 10, 5, 4, 3, 5 }, { 7, 3, 3, 2, 5 }, { 4, 2, 0, 0, 5 } },
		{ { 18, 6, 8, 4, 5 }, { 10, 4, 5, 3, 5 }, { 5, 3, 0, 0, 5 } } };

	/** A bladder standing on its neck: drawn in there, swelling to its girth above its middle and closing to a round
	 *  crown; UV x round it, y from its neck to its crown. */
	void Bladder(FBuild& B, const FVector& Neck, double Girth, double Tall, double NeckR, const FBulbDetail& D, float Rand, double Turn0)
	{
		TArray<FTurn> Wall;
		if (D.Rings <= 0)
		{
			Wall.Add({ 0.0, 0.0, 0.f });
			Wall.Add({ Girth * 0.95, Tall * 0.55, 0.55f });
			Wall.Add({ 0.0, Tall, 1.f });
		}
		else
		{
			for (int32 k = 0; k <= D.Rings; ++k)
			{
				const double T = (double)k / D.Rings;
				const double R = k == D.Rings ? 0.0 : FMath::Max(Girth * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Pow(T, 1.3)), 0.8), NeckR);
				Wall.Add({ R, Tall * T, (float)T });
			}
		}
		Lathe(B, Neck, Wall, D.Sides, EFace::Out, 1.f, Rand, Turn0);
	}

	void BuildBulb(const FFloraForm& F, int32 Lod, FBuild& B, FRaw& Raw)
	{
		const FBulbDetail& D = BulbDetail[ClassRow(F.Class)][FMath::Clamp(Lod, 0, 2)];
		FRandomStream Rng(7703 + 7919 * (int32)F.Variant);
		const bool bTrunk = TreeLike(F.Class);
		const bool bShrub = F.Class == EFloraClass::Shrub;
		const int32 Most = bTrunk || bShrub ? 5 : 3;
		const int32 Bulbs = FMath::Clamp(F.Parts == 0 ? (bShrub ? 4 : 3) : 1 + (int32)F.Parts, 1, Most);
		const double Girth = bTrunk ? FMath::Lerp(13.0, 28.0, (double)F.Spread) : (bShrub ? FMath::Lerp(12.0, 22.0, (double)F.Spread) : FMath::Lerp(8.0, 15.0, (double)F.Spread));
		const double Aspect = FMath::Lerp(1.9, 3.3, (double)F.Arch);
		const double FootZ = bTrunk ? -4.0 : -1.0;
		const double StalkR = bTrunk ? 1.3 + 0.05 * Girth : 0.8 + 0.03 * Girth;
		const double LeanA = Rng.FRandRange(0.f, UE_TWO_PI);
		const FVector LeanDir(FMath::Cos(LeanA), FMath::Sin(LeanA), 0.0);
		const double MainNeck = TopCm - Aspect * Girth;
		for (int32 i = 0; i < Bulbs; ++i)
		{
			const double A = LeanA + UE_DOUBLE_TWO_PI * (i + Rng.FRandRange(-0.2f, 0.2f)) / FMath::Max(Bulbs - 1, 1);
			const double Size = i == 0 ? 1.0 : (double)Rng.FRandRange(0.5f, 0.8f);
			const double Away = Rng.FRandRange(0.95f, 1.2f);
			const double Low = Rng.FRand();
			const float Rand = Rng.FRand();
			const double Turn = Rng.FRandRange(0.f, UE_TWO_PI);
			if (i >= D.MaxBulbs) { continue; }
			const double Gi = Girth * Size, Tall = Aspect * Gi;
			const FVector O(FMath::Cos(A), FMath::Sin(A), 0.0);
			// The first on the middle stalk, at the top; the rest out to the sides and lower, each clear of it.
			const FVector Neck = i == 0 ? LeanDir * ((double)F.Lean * 16.0) + FVector(0.0, 0.0, MainNeck)
				: O * ((Girth * 0.75 + Gi) * Away) + FVector(0.0, 0.0, FMath::Min(FMath::Lerp(18.0, FMath::Max(MainNeck * 0.85, 20.0), Low), TopCm - Tall - 4.0));
			const double Sr = StalkR * (i == 0 ? 1.0 : 0.7);
			if (D.StalkSides >= 3)
			{
				const FVector Foot(0.0, 0.0, FootZ);
				TArray<FVector> Path;
				TArray<double> Radii;
				for (int32 p = 0; p < D.StalkPts; ++p)
				{
					const double T = (double)p / (D.StalkPts - 1);
					// Out along the ground's side first and then up: a side stalk bows under its bladder.
					Path.Add(FVector(FMath::Lerp(Foot.X, Neck.X, FMath::Pow(T, 0.7)), FMath::Lerp(Foot.Y, Neck.Y, FMath::Pow(T, 0.7)), FMath::Lerp(Foot.Z, Neck.Z + 0.05 * Tall, T)));
					Radii.Add(Sr * FMath::Lerp(1.0, 0.7, T));
				}
				B.Tube(Path, Radii, D.StalkSides, 0.0, SwayAt(Neck.Z), Rand);
			}
			Bladder(B, Neck, Gi, Tall, Sr * 1.15, D, Rand, Turn);
		}
		Raw.TrunkR = StalkR;
		Raw.TrunkTop = MainNeck;
		Raw.WaistAt = 0.55;
	}

	void BuildRaw(const FFloraForm& F, int32 Lod, FBuild& B, FRaw& Raw)
	{
		switch (F.Plan)
		{
		case EFloraPlan::StalkCap: BuildStalkCap(F, Lod, B, Raw); break;
		case EFloraPlan::Fan: BuildFan(F, Lod, B, Raw); break;
		case EFloraPlan::Tubes: BuildTubes(F, Lod, B, Raw); break;
		default: BuildBulb(F, Lod, B, Raw); break;
		}
	}

	/** How a plant as laid out is fitted: scaled so its nearest level of detail stands to the unit height, and squeezed
	 *  or spread across so it is no narrower than the narrowest nor wider than the widest. Every level takes the same. */
	struct FFit
	{
		double Scale = 1.0, Across = 1.0, WideCm = 100.0;
		FRaw Raw;
	};
	FFit FitOf(const FFloraForm& F)
	{
		FBuild B;
		FFit Fit;
		BuildRaw(F, 0, B, Fit.Raw);
		double High = 1.0, Far = 1.0;
		for (const int32 V : B.Mesh.VertexIndicesItr())
		{
			const FVector P = (FVector)B.Mesh.GetVertex(V);
			High = FMath::Max(High, P.Z);
			Far = FMath::Max(Far, P.Size2D());
		}
		Fit.Scale = FitTopCm / High;
		const double Wide = 2.0 * Far * Fit.Scale;
		Fit.Across = Wide > WidestCm ? WidestCm / Wide : (Wide < NarrowestCm ? NarrowestCm / Wide : 1.0);
		Fit.WideCm = Wide * Fit.Across;
		return Fit;
	}
	void ApplyFit(FBuild& B, const FFit& Fit, bool bGiant)
	{
		for (const int32 V : B.Mesh.VertexIndicesItr())
		{
			const FVector P = (FVector)B.Mesh.GetVertex(V);
			B.Mesh.SetVertex(V, FVector3d(P.X * Fit.Scale * Fit.Across, P.Y * Fit.Scale * Fit.Across, P.Z * Fit.Scale));
		}
		if (bGiant)
		{
			for (const int32 E : B.C->ElementIndicesItr())
			{
				FVector4f Col = B.C->GetElement(E);
				Col.Y *= GiantSway;
				B.C->SetElement(E, Col);
			}
		}
	}

	/** A tree's or a giant's far crown: one closed shape of foliage round its top, two pyramids of Sides base to base
	 *  at the waist, lit as a mass. */
	void SolidCrown(FBuild& B, const FVector2D& Centre, double Bottom, double Waist, double Top, double Radius, int32 Sides)
	{
		const FVector C(Centre.X, Centre.Y, 0.0);
		const int32 Up = B.Vert(C + FVector(0.0, 0.0, Top), FVector::ZAxisVector, FVector2f(0.5f, 0.5f), FVector4f(1.f, SwayAt(Top), 0.5f, SolidSurface));
		const int32 Down = B.Vert(C + FVector(0.0, 0.0, Bottom), -FVector::ZAxisVector, FVector2f(0.5f, 0.5f), FVector4f(1.f, SwayAt(Bottom), 0.5f, SolidSurface));
		TArray<int32> Ring;
		for (int32 s = 0; s <= Sides; ++s)
		{
			const double A = UE_DOUBLE_TWO_PI * s / Sides;
			const FVector D(FMath::Cos(A), FMath::Sin(A), 0.0);
			Ring.Add(B.Vert(C + D * Radius + FVector(0.0, 0.0, Waist), D + FVector(0.0, 0.0, 0.35), FVector2f((float)s / Sides, 0.5f), FVector4f(1.f, SwayAt(Waist), 0.5f, SolidSurface)));
		}
		for (int32 s = 0; s < Sides; ++s)
		{
			TriN(B, Ring[s], Ring[s + 1], Up);
			// The under side by its own winding: its corners' normals lean up, as the crown is lit from above.
			const FVector Pa = (FVector)B.Mesh.GetVertex(Down), Pb = (FVector)B.Mesh.GetVertex(Ring[s]), Pc = (FVector)B.Mesh.GetVertex(Ring[s + 1]);
			const FVector Geo = FVector::CrossProduct(Pc - Pa, Pb - Pa);
			const FVector OutDown = (Pa + Pb + Pc) / 3.0 - (C + FVector(0.0, 0.0, Waist));
			if (FVector::DotProduct(Geo, OutDown) >= 0.0) { B.Tri(Down, Ring[s], Ring[s + 1]); }
			else { B.Tri(Down, Ring[s + 1], Ring[s]); }
		}
	}
}

bool SlonkStrangePlants::BuildLod(const FFloraForm& Form, int32 Lod, FDynamicMesh3& Out)
{
	using namespace SlonkStrange;
	if (Form.IsEarth()) { return false; }
	const bool bTree = TreeLike(Form.Class);
	if (Lod < 0 || Lod > (bTree ? 3 : 2)) { return false; }
	const FFit Fit = FitOf(Form);
	const bool bGiant = Form.Class == EFloraClass::Giant;
	FBuild B;
	if (Lod < 3)
	{
		FRaw Raw;
		BuildRaw(Form, Lod, B, Raw);
		ApplyFit(B, Fit, bGiant);
		Out = MoveTemp(B.Mesh);
		return true;
	}
	// The far crown, about the foliage of the nearest level as fitted.
	FBuild Near;
	FRaw Raw;
	BuildRaw(Form, 0, Near, Raw);
	ApplyFit(Near, Fit, false);
	FBox Foliage(ForceInit);
	for (const int32 V : Near.Mesh.VertexIndicesItr())
	{
		if (Near.C->GetElement(V).X >= 0.5f) { Foliage += (FVector)Near.Mesh.GetVertex(V); }
	}
	if (!Foliage.IsValid) { Foliage = FBox(FVector(-20.0, -20.0, 50.0), FVector(20.0, 20.0, FitTopCm)); }
	const FVector Mid = Foliage.GetCenter(), Half = Foliage.GetExtent();
	const double Radius = FMath::Max(0.9 * FMath::Min(Half.X, Half.Y), 2.0);
	const double High = Foliage.Max.Z;
	double Bottom = Foliage.Min.Z;
	if (Raw.bConeUnder) { Bottom = FMath::Max(FMath::Min(Bottom, High - 1.3 * Radius), 5.0); }
	Bottom = FMath::Min(Bottom, High - 4.0);
	const double Waist = FMath::Clamp(FMath::Lerp(Bottom, High, Raw.WaistAt), Bottom + 1.0, High - 1.0);
	SolidCrown(B, FVector2D(Mid.X, Mid.Y), Bottom, Waist, High, Radius, bGiant ? 7 : 5);
	if (bGiant)
	{
		for (const int32 E : B.C->ElementIndicesItr())
		{
			FVector4f Col = B.C->GetElement(E);
			Col.Y *= GiantSway;
			B.C->SetElement(E, Col);
		}
	}
	Out = MoveTemp(B.Mesh);
	return true;
}

double SlonkStrangePlants::WideCm(const FFloraForm& Form)
{
	return Form.IsEarth() ? 100.0 : SlonkStrange::FitOf(Form).WideCm;
}

double SlonkStrangePlants::FootCm(const FFloraForm& Form)
{
	using namespace SlonkStrange;
	if (Form.IsEarth()) { return 0.0; }
	// What of its nearest level of detail, as fitted, lies within an eighth of its height of the ground.
	const FFit Fit = FitOf(Form);
	FBuild B;
	FRaw Raw;
	BuildRaw(Form, 0, B, Raw);
	ApplyFit(B, Fit, false);
	double Far = 0.0;
	for (const int32 V : B.Mesh.VertexIndicesItr())
	{
		const FVector P = (FVector)B.Mesh.GetVertex(V);
		if (P.Z < 12.0) { Far = FMath::Max(Far, P.Size2D()); }
	}
	return Far;
}

bool SlonkStrangePlants::TrunkOf(const FFloraForm& Form, SlonkPlants::FTrunk& Out)
{
	using namespace SlonkStrange;
	if (Form.IsEarth() || !TreeLike(Form.Class)) { return false; }
	const FFit Fit = FitOf(Form);
	const double Radius = FMath::Max(Fit.Raw.TrunkR * Fit.Scale * Fit.Across, 1.0);
	const double Reach = FMath::Max(Fit.Raw.TrunkTop * Fit.Scale, 4.0 * Radius);
	Out.Radius = (float)Radius;
	Out.Length = (float)FMath::Max(Reach * 0.85 - 2.0 * Radius, 1.0);
	Out.Centre = FVector(0.0, 0.0, Reach * 0.45);
	return true;
}
