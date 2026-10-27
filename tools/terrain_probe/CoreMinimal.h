// Stand-in for the engine header so PlanetTerrain.cpp compiles on its own (tools/terrain_probe). Only what the
// height field uses: a double FVector, the FMath calls, the integer typedefs.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
typedef int32_t int32; typedef int64_t int64; typedef uint32_t uint32; typedef uint64_t uint64;
#define FORCEINLINE inline
#define SLONK_API
#define UE_DOUBLE_PI 3.14159265358979323846
struct FVector
{
	double X = 0, Y = 0, Z = 0;
	FVector() {}
	FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}
	static const FVector ZeroVector, ZAxisVector, XAxisVector;
	FVector operator+(const FVector& o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
	FVector operator-(const FVector& o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); }
	FVector operator*(double k) const { return FVector(X * k, Y * k, Z * k); }
	FVector operator/(double k) const { return FVector(X / k, Y / k, Z / k); }
	FVector operator-() const { return FVector(-X, -Y, -Z); }
	FVector& operator*=(double k) { X *= k; Y *= k; Z *= k; return *this; }
	FVector& operator+=(const FVector& o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
	bool operator==(const FVector& o) const { return X == o.X && Y == o.Y && Z == o.Z; }
	double Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	double SizeSquared() const { return X * X + Y * Y + Z * Z; }
	FVector GetSafeNormal() const { double s = Size(); return s > 1e-12 ? *this / s : FVector(); }
	bool IsNearlyZero(double tol = 1e-8) const { return std::fabs(X) <= tol && std::fabs(Y) <= tol && std::fabs(Z) <= tol; }
	static double DotProduct(const FVector& a, const FVector& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
	static FVector CrossProduct(const FVector& a, const FVector& b) { return FVector(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X); }
	static double Dist(const FVector& a, const FVector& b) { return (a - b).Size(); }
};
inline const FVector FVector::ZeroVector(0, 0, 0); inline const FVector FVector::ZAxisVector(0, 0, 1); inline const FVector FVector::XAxisVector(1, 0, 0);
inline FVector operator*(double k, const FVector& v) { return v * k; }
struct FMath
{
	static double Floor(double v) { return std::floor(v); }
	static double CeilToDouble(double v) { return std::ceil(v); }
	static double Lerp(double a, double b, double t) { return a + (b - a) * t; }
	template <class T> static T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
	template <class T> static T Max(T a, T b) { return a > b ? a : b; }
	template <class T> static T Min(T a, T b) { return a < b ? a : b; }
	static double Abs(double v) { return std::fabs(v); }
	static double Pow(double a, double b) { return std::pow(a, b); }
	static double Sqrt(double v) { return std::sqrt(v); }
	static double Loge(double v) { return std::log(v); }
	static double Acos(double v) { return std::acos(v); }
	static double Cos(double v) { return std::cos(v); }
	static double Sin(double v) { return std::sin(v); }
	static double Tan(double v) { return std::tan(v); }
	static bool IsNearlyZero(double v, double tol = 1e-8) { return std::fabs(v) <= tol; }
};
