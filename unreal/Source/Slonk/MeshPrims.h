// Slonk - small GeometryScript helpers shared by the procedural vessels: boxes and bodies of revolution, swept
// profiles, extruded outlines. Ship builders describe hulls as 2D profiles in these terms rather than as spheres
// and capsules, which is what lets them follow the real designs' lines.

#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "UDynamicMesh.h"

namespace SlonkMesh
{
	using PF = UGeometryScriptLibrary_MeshPrimitiveFunctions;

	/** Material slot stamped on every primitive appended from now on (0 = the hull's main material). Builders that
	 *  want accent colours set it around a group of appends and put it back to 0 when done. */
	inline int32& CurrentMaterialID()
	{
		static int32 ID = 0;
		return ID;
	}

	inline FGeometryScriptPrimitiveOptions Opts()
	{
		FGeometryScriptPrimitiveOptions O;
		O.MaterialID = CurrentMaterialID();
		return O;
	}

	/** Cylinder from A to B. */
	inline void CylinderBetween(UDynamicMesh* M, const FVector& A, const FVector& B, float Radius, int32 Steps = 10)
	{
		const FVector D = B - A;
		const float Len = D.Size();
		if (Len <= KINDA_SMALL_NUMBER) { return; }
		PF::AppendCylinder(M, Opts(), FTransform(FRotationMatrix::MakeFromZ(D / Len).Rotator(), A), Radius, Len, Steps, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	inline void Box(UDynamicMesh* M, const FVector& Centre, const FVector& Dims, const FRotator& Rot = FRotator::ZeroRotator)
	{
		PF::AppendBox(M, Opts(), FTransform(Rot, Centre), Dims.X, Dims.Y, Dims.Z, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
	}

	inline void Ellipsoid(UDynamicMesh* M, const FVector& Centre, const FVector& Radii, int32 Steps = 14)
	{
		PF::AppendSphereBox(M, Opts(), FTransform(FRotator::ZeroRotator, Centre, Radii), 1.f, Steps, Steps, Steps);
	}

	/** Capsule along local +X from Start. */
	inline void CapsuleX(UDynamicMesh* M, const FVector& Start, float Length, float Radius)
	{
		PF::AppendCapsule(M, Opts(), FTransform(FRotator(-90.f, 0.f, 0.f), Start), Radius, Length, 6, 16, 0, EGeometryScriptPrimitiveOriginMode::Base);
	}

	inline void CylinderZ(UDynamicMesh* M, const FVector& Base, float Radius, float Height, int32 Steps = 24)
	{
		PF::AppendCylinder(M, Opts(), FTransform(FRotator::ZeroRotator, Base), Radius, Height, Steps, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	/** Cylinder along +X from Start. */
	inline void CylinderX(UDynamicMesh* M, const FVector& Start, float Radius, float Length, int32 Steps = 24)
	{
		PF::AppendCylinder(M, Opts(), FTransform(FRotator(-90.f, 0.f, 0.f), Start), Radius, Length, Steps, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	inline void ConeZ(UDynamicMesh* M, const FVector& Base, float R0, float R1, float Height, int32 Steps = 24)
	{
		PF::AppendCone(M, Opts(), FTransform(FRotator::ZeroRotator, Base), R0, R1, Height, Steps, 1, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	/** Cone frustum along +X from Start: radius R0 at the start, R1 at the far end. */
	inline void ConeX(UDynamicMesh* M, const FVector& Start, float R0, float R1, float Length, int32 Steps = 24)
	{
		PF::AppendCone(M, Opts(), FTransform(FRotator(-90.f, 0.f, 0.f), Start), R0, R1, Length, Steps, 1, true, EGeometryScriptPrimitiveOriginMode::Base);
	}

	inline void TorusZ(UDynamicMesh* M, const FVector& Centre, float Major, float Minor, int32 MajorSteps = 48, int32 MinorSteps = 12)
	{
		FGeometryScriptRevolveOptions RO;
		PF::AppendTorus(M, Opts(), FTransform(FRotator::ZeroRotator, Centre), RO, Major, Minor, MajorSteps, MinorSteps, EGeometryScriptPrimitiveOriginMode::Center);
	}

	/** Winds a closed outline counter-clockwise, which is what the revolve and extrude primitives want. */
	inline void EnsureCCW(TArray<FVector2D>& P)
	{
		double Area = 0.0;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2D& A = P[i];
			const FVector2D& B = P[(i + 1) % P.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		if (Area < 0.0)
		{
			Algo::Reverse(P);
		}
	}

	/** Scales a unit outline: (r, h) pairs multiplied by (SR, SH). */
	inline TArray<FVector2D> Scaled(std::initializer_list<FVector2D> Pts, float SR, float SH)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Pts.size());
		for (const FVector2D& P : Pts)
		{
			Out.Add(FVector2D(P.X * SR, P.Y * SH));
		}
		return Out;
	}

	/** Body of revolution about the transform's local Z. Profile points are (radius, height). */
	inline void Revolve(UDynamicMesh* M, const FTransform& Xf, TArray<FVector2D> Profile, int32 Steps = 32)
	{
		EnsureCCW(Profile);
		FGeometryScriptRevolveOptions RO;
		PF::AppendRevolvePolygon(M, Opts(), Xf, Profile, RO, 0.f, Steps);
	}

	/** Revolve about a vertical axis through Centre; Scale stretches the result along local X/Y (an oval saucer). */
	inline void RevolveZ(UDynamicMesh* M, const FVector& Centre, TArray<FVector2D> Profile, int32 Steps = 32, const FVector& Scale = FVector::OneVector)
	{
		Revolve(M, FTransform(FRotator::ZeroRotator, Centre, Scale), MoveTemp(Profile), Steps);
	}

	/** Revolve about the ship's long axis (+X) through Centre. Profile is (radius, x). Height/Width flatten the
	 *  section: Height scales the vertical radius, Width the sideways one. */
	inline void RevolveX(UDynamicMesh* M, const FVector& Centre, TArray<FVector2D> Profile, int32 Steps = 24, float Height = 1.f, float Width = 1.f)
	{
		// Pitch -90 turns local Z onto +X; local X then lies along -Z (height) and local Y stays sideways.
		Revolve(M, FTransform(FRotator(-90.f, 0.f, 0.f), Centre, FVector(Height, Width, 1.f)), MoveTemp(Profile), Steps);
	}

	/** Flat plate: Outline in the plane spanned by AxisU/AxisV through Origin, Thickness centred on that plane. */
	inline void Extrude(UDynamicMesh* M, const FVector& Origin, const FVector& AxisU, const FVector& AxisV, TArray<FVector2D> Outline, float Thickness)
	{
		EnsureCCW(Outline);
		const FRotator Rot = FRotationMatrix::MakeFromXY(AxisU.GetSafeNormal(), AxisV.GetSafeNormal()).Rotator();
		PF::AppendSimpleExtrudePolygon(M, Opts(), FTransform(Rot, Origin), Outline, Thickness, 0, true, EGeometryScriptPrimitiveOriginMode::Center);
	}

	/** Rounded rectangle outline centred on the origin. */
	inline TArray<FVector2D> RoundRect(float HalfU, float HalfV, float Radius, int32 CornerSteps = 4)
	{
		TArray<FVector2D> Out;
		Radius = FMath::Min(Radius, FMath::Min(HalfU, HalfV) * 0.999f);
		const float Su[4] = { 1.f, -1.f, -1.f, 1.f };
		const float Sv[4] = { 1.f, 1.f, -1.f, -1.f };
		for (int32 c = 0; c < 4; ++c)
		{
			const FVector2D Pivot(Su[c] * (HalfU - Radius), Sv[c] * (HalfV - Radius));
			for (int32 k = 0; k <= CornerSteps; ++k)
			{
				const float A = FMath::DegreesToRadians(90.f * c + 90.f * k / CornerSteps);
				Out.Add(Pivot + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
		}
		return Out;
	}

	/** A quarter-circle arc from angle A0 to A1 (degrees) around Pivot, appended to Out. */
	inline void Arc(TArray<FVector2D>& Out, const FVector2D& Pivot, float Radius, float A0, float A1, int32 Steps = 4)
	{
		for (int32 k = 0; k <= Steps; ++k)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, (float)k / Steps));
			Out.Add(Pivot + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
	}
}
