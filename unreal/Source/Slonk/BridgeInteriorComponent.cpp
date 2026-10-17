#include "BridgeInteriorComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

UBridgeInteriorComponent::UBridgeInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (CubeFinder.Succeeded()) { CubeMesh = CubeFinder.Object; }
	if (CylFinder.Succeeded()) { CylinderMesh = CylFinder.Object; }
	if (SphereFinder.Succeeded()) { SphereMesh = SphereFinder.Object; }

	LitMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeLit.M_BridgeLit")));
	DisplayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeDisplay.M_BridgeDisplay")));
	ButtonsMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeButtons.M_BridgeButtons")));
	ViewerMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeViewer.M_BridgeViewer")));
}

void UBridgeInteriorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsBuilt())
	{
		Build();
	}
}

// ---- Materials --------------------------------------------------------------------------------

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic, FLinearColor Emissive, float EmissiveStrength)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(LitMaterial, TEXT("BridgeLit"), true);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_%s"), Name));
	Mid->SetVectorParameterValue(TEXT("BaseColor"), Base);
	Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	Mid->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	Mid->SetVectorParameterValue(TEXT("EmissiveColor"), Emissive);
	Mid->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
	Materials.Add(Mid);
	return Mid;
}

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeScreen(const TSoftObjectPtr<UMaterialInterface>& ParentSoft, const TCHAR* Name, float Seed, float Intensity, float CellScale)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(ParentSoft, Name);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_%s_%d"), Name, PieceCount));
	Mid->SetScalarParameterValue(TEXT("Seed"), Seed);
	Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Mid->SetScalarParameterValue(TEXT("CellScale"), CellScale);
	Materials.Add(Mid);
	return Mid;
}

// ---- Primitives -------------------------------------------------------------------------------

void UBridgeInteriorComponent::PrepPrimitive(UPrimitiveComponent* C, bool bShadow)
{
	C->SetupAttachment(this);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetMobility(EComponentMobility::Movable);
	C->CastShadow = bShadow;
	C->bCastDynamicShadow = bShadow;
	C->bAffectDistanceFieldLighting = false;
	C->SetReceivesDecals(false);
	C->SetVisibility(IsVisible());
}

UStaticMeshComponent* UBridgeInteriorComponent::Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Bridge_%d"), PieceCount++));
	PrepPrimitive(C, bShadow);
	C->SetStaticMesh(Mesh);
	// Engine basic shapes are 100 cm across.
	C->SetRelativeLocationAndRotation(Center, Rot);
	C->SetRelativeScale3D(Size / 100.0);
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	C->RegisterComponent();
	Pieces.Add(C);
	return C;
}

UDynamicMeshComponent* UBridgeInteriorComponent::Revolve(const TArray<FVector2D>& Profile, float StartDeg, float EndDeg, UMaterialInterface* Mat, bool bShadow)
{
	UDynamicMeshComponent* C = NewObject<UDynamicMeshComponent>(GetOwner(), *FString::Printf(TEXT("BridgeRing_%d"), PieceCount++));
	PrepPrimitive(C, bShadow);
	UDynamicMesh* Mesh = C->GetDynamicMesh();
	if (Mesh)
	{
		Mesh->Reset();
		FGeometryScriptPrimitiveOptions Options;
		FGeometryScriptRevolveOptions Rev;
		Rev.RevolveDegrees = EndDeg - StartDeg;
		Rev.DegreeOffset = StartDeg;
		Rev.bHardNormals = true;
		Rev.HardNormalAngle = 35.f;
		const int32 Steps = FMath::Max(4, FMath::RoundToInt(FMath::Abs(Rev.RevolveDegrees) / 3.f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePolygon(Mesh, Options, FTransform::Identity, Profile, Rev, 0.f, Steps);
		FGeometryScriptCalculateNormalsOptions NormalOptions;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, NormalOptions);
		C->NotifyMeshUpdated();
	}
	C->RegisterComponent();
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	Pieces.Add(C);
	return C;
}

void UBridgeInteriorComponent::Light(const FVector& Pos, float Intensity, float Radius)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(GetOwner(), *FString::Printf(TEXT("BridgeLight_%d"), PieceCount++));
	L->SetupAttachment(this);
	L->SetMobility(EComponentMobility::Movable);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetLightColor(LightColor);
	L->SetAttenuationRadius(Radius);
	L->SetSourceRadius(20.f);
	L->SetSoftSourceRadius(40.f);
	L->SetCastShadows(false);
	L->bUseInverseSquaredFalloff = true;
	L->SetRelativeLocation(Pos);
	L->SetVisibility(IsVisible());
	L->RegisterComponent();
	Pieces.Add(L);
}

UStaticMeshComponent* UBridgeInteriorComponent::WallBox(float AngleDeg, float Radial, float Z, float Thickness, float Width, float Height, UMaterialInterface* Mat, float PitchDeg, bool bShadow)
{
	const FRotator Rot(PitchDeg, AngleDeg, 0.f);
	const FVector Dir = FRotator(0.f, AngleDeg, 0.f).Vector();
	return Box(Dir * Radial + FVector(0, 0, Z), FVector(Thickness, Width, Height), Rot, Mat, bShadow);
}

// ---- Furniture --------------------------------------------------------------------------------

void UBridgeInteriorComponent::Crew(const FVector& SeatPos, float YawDeg, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	Cyl(SeatPos + FVector(0, 0, 36), 19.f, 60.f, UniformMat, R);
	Box(SeatPos + FVector(0, 0, 60), FVector(26, 48, 14), R, UniformMat);
	Cyl(SeatPos + FVector(0, 0, 76), 7.f, 12.f, Skin, R);
	Ball(SeatPos + FVector(0, 0, 94), 12.f, Skin);
	Ball(SeatPos + FVector(0, 0, 96.5f) - F * 2.5f, 12.f, Hair);
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, -11, 0)), FVector(44, 16, 14), R, ChairBlack);
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, 11, 0)), FVector(44, 16, 14), R, ChairBlack);
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, -26, 0)), FVector(34, 9, 9), R, UniformMat);
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, 26, 0)), FVector(34, 9, 9), R, UniformMat);
}

void UBridgeInteriorComponent::SwivelChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Four-spoke white base, white pedestal, black moulded seat with a tall wrap-around back.
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = YawDeg + 45.f + i * 90.f;
		Box(FloorPos + FRotator(0.f, A, 0.f).Vector() * 16.f + FVector(0, 0, 2.5f), FVector(34, 6, 5), FRotator(0.f, A, 0.f), LightGrey);
	}
	Cyl(FloorPos + FVector(0, 0, 24), 5.f, 44.f, LightGrey, R);
	Box(FloorPos + FVector(0, 0, 49), FVector(50, 50, 9), R, ChairBlack);
	Box(FloorPos - F * 24.f + FVector(0, 0, 82), FVector(7, 50, 62), R, ChairBlack);
	Box(FloorPos - F * 14.f + FVector(0, 0, 70) + R.RotateVector(FVector(0, -25, 0)), FVector(26, 6, 38), R, ChairBlack);
	Box(FloorPos - F * 14.f + FVector(0, 0, 70) + R.RotateVector(FVector(0, 25, 0)), FVector(26, 6, 38), R, ChairBlack);
	if (bCrew && bShowCrew)
	{
		Crew(FloorPos + FVector(0, 0, 54.f), YawDeg, UniformMat ? UniformMat : ChairBlack);
	}
}

// ---- Build ------------------------------------------------------------------------------------

void UBridgeInteriorComponent::Build()
{
	for (USceneComponent* P : Pieces)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	Pieces.Reset();
	Materials.Reset();
	PieceCount = 0;

	WallBlue = MakeLit(TEXT("WallBlue"), FLinearColor(0.26f, 0.32f, 0.42f), 0.75f);
	CeilingGrey = MakeLit(TEXT("CeilingGrey"), FLinearColor(0.16f, 0.18f, 0.22f), 0.85f);
	Deck = MakeLit(TEXT("Deck"), FLinearColor(0.27f, 0.28f, 0.30f), 0.9f);
	WellFloor = MakeLit(TEXT("WellFloor"), FLinearColor(0.20f, 0.21f, 0.23f), 0.9f);
	ConsoleBlack = MakeLit(TEXT("ConsoleBlack"), FLinearColor(0.03f, 0.03f, 0.035f), 0.5f);
	Red = MakeLit(TEXT("Red"), FLinearColor(0.72f, 0.03f, 0.03f), 0.45f);
	LightGrey = MakeLit(TEXT("LightGrey"), FLinearColor(0.62f, 0.62f, 0.64f), 0.5f);
	ChairBlack = MakeLit(TEXT("ChairBlack"), FLinearColor(0.045f, 0.045f, 0.05f), 0.65f);
	Skin = MakeLit(TEXT("Skin"), FLinearColor(0.55f, 0.38f, 0.28f), 0.6f);
	Hair = MakeLit(TEXT("Hair"), FLinearColor(0.08f, 0.05f, 0.03f), 0.7f);
	LightPanel = MakeLit(TEXT("LightPanel"), FLinearColor(0.9f, 0.9f, 0.9f), 0.9f, 0.f, FLinearColor(1.0f, 0.96f, 0.9f), LightPanelEmissive);
	UniformGold = MakeLit(TEXT("UniformGold"), FLinearColor(0.50f, 0.36f, 0.06f), 0.85f);
	UniformBlue = MakeLit(TEXT("UniformBlue"), FLinearColor(0.08f, 0.22f, 0.55f), 0.85f);
	UniformRed = MakeLit(TEXT("UniformRed"), FLinearColor(0.50f, 0.05f, 0.07f), 0.85f);

	if (Style == EBridgeStyle::Refit)
	{
		BuildRefit();
	}
	else
	{
		BuildOriginal();
	}
}

void UBridgeInteriorComponent::BuildOriginal()
{
	const float R = WallRadius;
	const float RD = DeckRadius;
	const float HD = DeckHeight;
	const float H = CeilingHeight;
	const float VS = ViewscreenHalfAngle;
	const float TL0 = TurboliftAngle - TurboliftHalfAngle;
	const float TL1 = TurboliftAngle + TurboliftHalfAngle;

	// ---- Floors: central well, raised outer deck ring with its red edging, two steps down at the front.
	Cyl(FVector(0, 0, -3), RD + 10.f, 6.f, WellFloor);
	Revolve({ FVector2D(RD, 0), FVector2D(R + 15.f, 0), FVector2D(R + 15.f, HD), FVector2D(RD, HD) }, 0.f, 360.f, Deck);
	Revolve({ FVector2D(RD - 4.f, HD - 6.f), FVector2D(RD + 6.f, HD - 6.f), FVector2D(RD + 6.f, HD + 1.2f), FVector2D(RD - 4.f, HD + 1.2f) }, 0.f, 360.f, Red, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		const float A = s * (VS + 6.f);
		WallBox(A, RD - 24.f, HD * 0.25f, 46.f, 110.f, HD * 0.5f, Deck);
		WallBox(A, RD - 24.f, HD * 0.5f - 2.f, 46.f, 110.f, 4.f, Red, 0.f, false);
	}

	// ---- Handrail on the deck edge, in four arcs with gaps fore, aft and abeam.
	{
		const float RR = RD + 17.f;
		const float RailZ = HD + 92.f;
		const float Arcs[4][2] = { { 26.f, 86.f }, { 104.f, 170.f }, { 190.f, 256.f }, { 274.f, 334.f } };
		for (const auto& Arc : Arcs)
		{
			Revolve({ FVector2D(RR - 3.5f, RailZ - 4.f), FVector2D(RR + 3.5f, RailZ - 4.f), FVector2D(RR + 3.5f, RailZ + 4.f), FVector2D(RR - 3.5f, RailZ + 4.f) }, Arc[0], Arc[1], Red, false);
			const int32 Posts = FMath::Max(2, FMath::RoundToInt((Arc[1] - Arc[0]) / 15.f) + 1);
			for (int32 i = 0; i < Posts; ++i)
			{
				const float A = FMath::Lerp(Arc[0], Arc[1], (float)i / (Posts - 1));
				Cyl(FRotator(0.f, A, 0.f).Vector() * RR + FVector(0, 0, HD + 46.f), 2.4f, 92.f, ConsoleBlack, FRotator::ZeroRotator, false);
			}
		}
	}

	// ---- Perimeter console ring: black body with a sloped top, broken at the viewscreen and the turbolift.
	{
		const TArray<FVector2D> Body = { FVector2D(R - 58.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, HD + 82.f), FVector2D(R - 58.f, HD + 58.f) };
		Revolve(Body, VS, TL0, ConsoleBlack);
		Revolve(Body, TL1, 360.f - VS, ConsoleBlack);
	}
	// Monitor band and upper wall (the upper wall also runs over the turbolift).
	const TArray<FVector2D> MonitorBand = { FVector2D(R + 2.f, HD + 82.f), FVector2D(R + 12.f, HD + 82.f), FVector2D(R + 12.f, HD + 160.f), FVector2D(R + 2.f, HD + 160.f) };
	Revolve(MonitorBand, VS, TL0, ConsoleBlack);
	Revolve(MonitorBand, TL1, 360.f - VS, ConsoleBlack);
	Revolve({ FVector2D(R + 4.f, HD + 160.f), FVector2D(R + 16.f, HD + 160.f), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 4.f, H + 2.f) }, VS, 360.f - VS, WallBlue);
	// Light-grey trim bands at the console lip and above the monitors.
	const TArray<FVector2D> Lip = { FVector2D(R - 60.f, HD + 56.f), FVector2D(R - 56.f, HD + 56.f), FVector2D(R - 56.f, HD + 61.f), FVector2D(R - 60.f, HD + 61.f) };
	Revolve(Lip, VS, TL0, LightGrey, false);
	Revolve(Lip, TL1, 360.f - VS, LightGrey, false);
	Revolve({ FVector2D(R - 1.f, HD + 158.f), FVector2D(R + 5.f, HD + 158.f), FVector2D(R + 5.f, HD + 164.f), FVector2D(R - 1.f, HD + 164.f) }, VS, 360.f - VS, LightGrey, false);

	// Ceiling: dark outer disc, light-blue inner ring, glowing light ring.
	Cyl(FVector(0, 0, H + 6.f), R + 20.f, 12.f, ConsoleBlack);
	Revolve({ FVector2D(0.f, H - 1.f), FVector2D(R * 0.62f, H - 1.f), FVector2D(R * 0.62f, H + 1.f), FVector2D(0.f, H + 1.f) }, 0.f, 360.f, CeilingGrey);
	Revolve({ FVector2D(R * 0.62f, H - 4.f), FVector2D(R * 0.74f, H - 4.f), FVector2D(R * 0.74f, H), FVector2D(R * 0.62f, H) }, 0.f, 360.f, LightPanel, false);
	Revolve({ FVector2D(R * 0.74f, H - 2.f), FVector2D(R + 4.f, H - 2.f), FVector2D(R + 4.f, H), FVector2D(R * 0.74f, H) }, 0.f, 360.f, CeilingGrey);

	// ---- Wall bays: button panels on the console slope, monitors above, framed viewers on the upper wall.
	const int32 N = FMath::Max(8, WallBays);
	const float Step = 360.f / N;
	const float Chord = 2.f * R * FMath::Sin(FMath::DegreesToRadians(Step * 0.5f));
	const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan2(24.f, 70.f));
	float Seed = 1.f;
	for (int32 k = 0; k < N; ++k)
	{
		const float A = FMath::Fmod(k * Step + Step * 0.5f, 360.f);
		const bool bScreen = (A < VS) || (A > 360.f - VS);
		const bool bLift = (A > TL0 && A < TL1);
		if (bScreen)
		{
			continue;
		}
		if (!bLift)
		{
			// Sloped lamp panel sitting just proud of the console top (slope runs from R-58 at +58 to R+12 at +82).
			const FVector Mid = FRotator(0.f, A, 0.f).Vector() * (R - 23.f) + FVector(0, 0, HD + 70.f);
			const FVector Up = FRotator(0.f, A, 0.f).RotateVector(FVector(-24.f, 0.f, 70.f).GetSafeNormal());
			Box(Mid + Up * 1.2f, FVector(66.f, Chord - 26.f, 2.f), FRotator(-SlopeDeg, A, 0.f), MakeScreen(ButtonsMaterial, TEXT("Buttons"), Seed, DisplayIntensity, 1.f), false);
			WallBox(A, R + 1.f, HD + 121.f, 3.f, Chord - 22.f, 64.f, MakeScreen(DisplayMaterial, TEXT("Monitor"), Seed + 0.5f, DisplayIntensity * 0.8f, 1.f), 0.f, false);
		}
		// Overhead viewer with a pale frame.
		WallBox(A, R + 3.f, HD + 215.f, 5.f, FMath::Min(Chord - 40.f, 150.f), 92.f, LightGrey, 0.f, false);
		WallBox(A, R + 0.f, HD + 215.f, 3.f, FMath::Min(Chord - 56.f, 134.f), 78.f, MakeScreen(ViewerMaterial, TEXT("Viewer"), Seed + 3.f, DisplayIntensity * 0.7f, 1.f), 0.f, false);
		Seed += 7.f;
	}

	// ---- Viewscreen: console-height sill, lintel above, pale pillars either side (the opening itself is empty).
	Revolve({ FVector2D(R + 2.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, ViewscreenBottom), FVector2D(R + 2.f, ViewscreenBottom) }, -VS, VS, ConsoleBlack);
	Revolve({ FVector2D(R - 2.f, ViewscreenBottom - 6.f), FVector2D(R + 6.f, ViewscreenBottom - 6.f), FVector2D(R + 6.f, ViewscreenBottom), FVector2D(R - 2.f, ViewscreenBottom) }, -VS, VS, LightGrey, false);
	Revolve({ FVector2D(R + 2.f, ViewscreenTop), FVector2D(R + 16.f, ViewscreenTop), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 2.f, H + 2.f) }, -VS, VS, WallBlue);
	Revolve({ FVector2D(R - 2.f, ViewscreenTop), FVector2D(R + 6.f, ViewscreenTop), FVector2D(R + 6.f, ViewscreenTop + 6.f), FVector2D(R - 2.f, ViewscreenTop + 6.f) }, -VS, VS, LightGrey, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		WallBox(s * (VS + 1.5f), R + 4.f, (HD + H) * 0.5f, 26.f, 26.f, H - HD, LightGrey);
	}

	// ---- Turbolift alcove: recessed pale doors with a red surround.
	{
		const float A = TurboliftAngle;
		WallBox(A, R + 8.f, HD + 100.f, 6.f, 180.f, 200.f, Red);
		WallBox(A, R + 4.f, HD + 100.f, 4.f, 150.f, 190.f, LightGrey);
		WallBox(A, R + 1.5f, HD + 100.f, 2.f, 3.f, 190.f, ConsoleBlack, 0.f, false);
		// Console-height wall stubs either side of the doors.
		for (int32 s = -1; s <= 1; s += 2)
		{
			WallBox(A + s * TurboliftHalfAngle, R - 20.f, HD + 40.f, 70.f, 10.f, 80.f, ConsoleBlack);
		}
	}

	// ---- Helm (port) and navigation (starboard): one black wedge console in the well, sloped top, astrogator dome.
	{
		const float X0 = 185.f;
		Box(FVector(X0 + 42.f, 0, 42.f), FVector(84, 250, 84), FRotator::ZeroRotator, ConsoleBlack);
		Box(FVector(X0 + 42.f, 0, 92.f), FVector(90, 256, 16), FRotator(-14.f, 0.f, 0.f), ConsoleBlack);
		Box(FVector(X0 + 40.f, -66.f, 101.f), FVector(60, 96, 2), FRotator(-14.f, 0.f, 0.f), MakeScreen(ButtonsMaterial, TEXT("Helm"), 101.f, DisplayIntensity, 0.8f), false);
		Box(FVector(X0 + 40.f, 66.f, 101.f), FVector(60, 96, 2), FRotator(-14.f, 0.f, 0.f), MakeScreen(ButtonsMaterial, TEXT("Nav"), 102.f, DisplayIntensity, 0.8f), false);
		Ball(FVector(X0 + 48.f, 0, 100.f), 15.f, LightGrey);
		Box(FVector(X0 + 36.f, 0, 100.f), FVector(10, 36, 8), FRotator::ZeroRotator, ChairBlack);
		Box(FVector(X0 + 2.f, 0, 60.f), FVector(4, 250, 3), FRotator::ZeroRotator, LightGrey, false);
		SwivelChair(FVector(118.f, -64.f, 0.f), 0.f, true, UniformGold);
		SwivelChair(FVector(118.f, 64.f, 0.f), 0.f, true, UniformGold);
	}

	// ---- Captain's chair on a round platform at the centre, camera at the headrest.
	Cyl(FVector(0, 0, 15.f), 120.f, 30.f, Deck);
	Revolve({ FVector2D(118.f, 24.f), FVector2D(124.f, 24.f), FVector2D(124.f, 31.f), FVector2D(118.f, 31.f) }, 0.f, 360.f, LightGrey, false);
	Box(FVector(-10.f, 0, 46.f), FVector(64, 62, 32), FRotator::ZeroRotator, LightGrey);
	Box(FVector(-6.f, 0, 68.f), FVector(64, 68, 12), FRotator::ZeroRotator, ChairBlack);
	Box(FVector(-36.f, 0, 112.f), FVector(12, 68, 80), FRotator(-6.f, 0.f, 0.f), ChairBlack);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(-8.f, s * 40.f, 92.f), FVector(64, 14, 12), FRotator::ZeroRotator, LightGrey);
		Box(FVector(10.f, s * 40.f, 98.5f), FVector(22, 12, 1.5f), FRotator::ZeroRotator, MakeScreen(ButtonsMaterial, TEXT("Arm"), 200.f + s, DisplayIntensity * 0.8f, 0.5f), false);
	}

	// ---- Perimeter stations: swivel chairs facing their consoles.
	struct FSeat { float Angle; bool bCrew; UMaterialInterface* Uniform; };
	const FSeat Seats[] = {
		{ 52.f, false, nullptr }, { 82.f, true, UniformBlue }, { 112.f, true, UniformBlue },
		{ 180.f, false, nullptr }, { 212.f, true, UniformRed }, { 248.f, true, UniformRed },
		{ 280.f, false, nullptr }, { 308.f, true, UniformGold } };
	for (const FSeat& S : Seats)
	{
		if (S.Angle > TL0 - 8.f && S.Angle < TL1 + 8.f)
		{
			continue;
		}
		SwivelChair(FRotator(0.f, S.Angle, 0.f).Vector() * (R - 128.f) + FVector(0, 0, HD), S.Angle, S.bCrew, S.Uniform);
	}

	// ---- Lighting: even overhead fill from a ring of points plus the glowing ceiling ring.
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * 60.f + 30.f;
		Light(FRotator(0.f, A, 0.f).Vector() * (R * 0.55f) + FVector(0, 0, H - 120.f), LightIntensity, 1300.f);
	}
	Light(FVector(0, 0, H - 130.f), LightIntensity * 0.5f, 1200.f);
}

// ---- The Enterprise-A bridge of Star Trek VI --------------------------------------------------
//
// Light-grey panelling throughout, a charcoal carpet, white light strips along the feet of the consoles and
// under the deck lip, raked banks of teal and green displays round the perimeter, grey upholstered chairs on
// black pedestals, a raised command platform ringed by a grey handrail with the captain's chair at its centre
// and the single curved helm/navigation console a step down in front of it, burgundy turbolift doors aft with a
// burgundy bench between them. Same coordinate frame as the original: origin under the captain's
// chair, +X toward the viewscreen, yaw 0 forward.

void UBridgeInteriorComponent::RefitChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Round black base, black pedestal, grey upholstered seat and a wrap-around back with black arms.
	Cyl(FloorPos + FVector(0, 0, 2.5f), 26.f, 5.f, ChairBlack, R);
	Cyl(FloorPos + FVector(0, 0, 25), 5.f, 44.f, ChairBlack, R);
	Box(FloorPos + FVector(0, 0, 50), FVector(48, 48, 10), R, SeatGrey);
	Box(FloorPos - F * 22.f + FVector(0, 0, 84), FVector(9, 46, 66), R, SeatGrey);
	Box(FloorPos - F * 22.f + FVector(0, 0, 84) - F * 5.f, FVector(3, 50, 70), R, ChairBlack);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FloorPos - F * 10.f + FVector(0, 0, 72) + R.RotateVector(FVector(0, s * 26.f, 0)), FVector(30, 4, 4), R, ChairBlack);
		Box(FloorPos - F * 20.f + FVector(0, 0, 63) + R.RotateVector(FVector(0, s * 26.f, 0)), FVector(4, 4, 20), R, ChairBlack);
	}
	if (bCrew && bShowCrew)
	{
		Crew(FloorPos + FVector(0, 0, 55.f), YawDeg, UniformMat ? UniformMat : ChairBlack);
	}
}

void UBridgeInteriorComponent::RefitStation(float A, float Chord, float& Seed, bool bConsole)
{
	const float R = WallRadius;
	const float HD = DeckHeight;
	if (bConsole)
	{
		// Console body: a light-grey block off the wall with a dark raked display bank on top, a recessed black
		// kick with a white light strip at the foot, and a thin light strip along the lip.
		WallBox(A, R - 44.f, HD + 36.f, 60.f, Chord - 14.f, 72.f, PanelGrey);
		WallBox(A, R - 76.f, HD + 7.f, 6.f, Chord - 20.f, 14.f, ChairBlack, 0.f, false);
		WallBox(A, R - 74.f, HD + 6.f, 3.f, Chord - 24.f, 5.f, StripWhite, 0.f, false);
		WallBox(A, R - 74.f, HD + 73.f, 4.f, Chord - 14.f, 3.f, StripWhite, 0.f, false);
		// Desk: dark top with a row of lit touch panels, then the raked bank of displays leaning against the wall.
		WallBox(A, R - 50.f, HD + 73.5f, 46.f, Chord - 18.f, 3.f, ScreenDark, 0.f, false);
		UMaterialInstanceDynamic* Touch = MakeScreen(ButtonsMaterial, TEXT("RefitTouch"), Seed, DisplayIntensity * 0.9f, 0.8f);
		WallBox(A, R - 52.f, HD + 75.6f, 34.f, Chord - 30.f, 1.5f, Touch, 0.f, false);
		WallBox(A, R - 14.f, HD + 112.f, 10.f, Chord - 14.f, 84.f, ScreenDark, -18.f, false);
		UMaterialInstanceDynamic* Bank = MakeScreen(DisplayMaterial, TEXT("RefitBank"), Seed + 0.5f, DisplayIntensity, 2.2f);
		if (Bank)
		{
			Bank->SetVectorParameterValue(TEXT("ColorA"), FLinearColor(0.25f, 0.95f, 0.8f));
			Bank->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(0.2f, 0.7f, 0.9f));
			Bank->SetVectorParameterValue(TEXT("ColorC"), FLinearColor(0.35f, 0.9f, 0.4f));
			Bank->SetVectorParameterValue(TEXT("ColorD"), FLinearColor(0.9f, 0.9f, 0.95f));
		}
		WallBox(A, R - 20.2f, HD + 112.f, 2.f, Chord - 24.f, 72.f, Bank, -18.f, false);
	}
	// Upper wall monitor in a pale frame, with a light strip under it.
	WallBox(A, R + 2.f, HD + 200.f, 6.f, FMath::Min(Chord - 26.f, 150.f), 78.f, LightGrey, 0.f, false);
	UMaterialInstanceDynamic* Mon = MakeScreen(ViewerMaterial, TEXT("RefitMonitor"), Seed + 3.f, DisplayIntensity * 0.7f, 1.f);
	WallBox(A, R - 1.f, HD + 200.f, 3.f, FMath::Min(Chord - 40.f, 136.f), 64.f, Mon, 0.f, false);
	WallBox(A, R - 1.f, HD + 157.f, 3.f, FMath::Min(Chord - 40.f, 136.f), 3.f, StripWhite, 0.f, false);
	Seed += 7.f;
}

void UBridgeInteriorComponent::BuildRefit()
{
	PanelGrey = MakeLit(TEXT("PanelGrey"), FLinearColor(0.50f, 0.51f, 0.53f), 0.6f);
	PanelDark = MakeLit(TEXT("PanelDark"), FLinearColor(0.22f, 0.23f, 0.25f), 0.7f);
	Carpet = MakeLit(TEXT("Carpet"), FLinearColor(0.075f, 0.08f, 0.085f), 0.95f);
	StripWhite = MakeLit(TEXT("StripWhite"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), LightPanelEmissive * 2.5f);
	Burgundy = MakeLit(TEXT("Burgundy"), FLinearColor(0.30f, 0.05f, 0.06f), 0.7f);
	SeatGrey = MakeLit(TEXT("SeatGrey"), FLinearColor(0.55f, 0.55f, 0.56f), 0.9f);
	RailGrey = MakeLit(TEXT("RailGrey"), FLinearColor(0.42f, 0.42f, 0.44f), 0.35f, 0.4f);
	ScreenDark = MakeLit(TEXT("ScreenDark"), FLinearColor(0.02f, 0.025f, 0.03f), 0.35f);
	UniformMaroon = MakeLit(TEXT("UniformMaroon"), FLinearColor(0.40f, 0.07f, 0.09f), 0.85f);

	const float R = WallRadius;
	const float HD = DeckHeight;          // perimeter deck above the helm floor
	const float HC = HD + 26.f;           // command platform
	const float H = CeilingHeight;
	const float VS = ViewscreenHalfAngle;
	const float RC = 215.f;               // command platform radius
	const float RD = DeckRadius;          // inner edge of the perimeter deck
	const float Lifts[2] = { 150.f, 210.f };
	const float LiftHalf = 12.f;

	// ---- Floors: carpet everywhere. Helm floor, perimeter deck ring, two-tier command platform behind the helm.
	Cyl(FVector(0, 0, -3), RD + 10.f, 6.f, Carpet);
	Revolve({ FVector2D(RD, 0), FVector2D(R + 15.f, 0), FVector2D(R + 15.f, HD), FVector2D(RD, HD) }, 0.f, 360.f, Carpet);
	Revolve({ FVector2D(RD - 1.f, HD - 4.f), FVector2D(RD + 3.f, HD - 4.f), FVector2D(RD + 3.f, HD + 0.5f), FVector2D(RD - 1.f, HD + 0.5f) }, 0.f, 360.f, StripWhite, false);
	// Command platform: a horseshoe open at the front (the helm sits in the gap), its edge lit from below.
	Revolve({ FVector2D(0.f, 0.f), FVector2D(RC, 0.f), FVector2D(RC, HC), FVector2D(0.f, HC) }, 48.f, 312.f, Carpet);
	Revolve({ FVector2D(RC - 2.f, HC - 5.f), FVector2D(RC + 1.5f, HC - 5.f), FVector2D(RC + 1.5f, HC - 1.f), FVector2D(RC - 2.f, HC - 1.f) }, 48.f, 312.f, StripWhite, false);
	// The platform's forward edge is squared off either side of the helm, with a two-step stair down the middle.
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(100.f, s * 170.f, HC * 0.5f), FVector(90.f, 110.f, HC), FRotator::ZeroRotator, Carpet);
	}
	Box(FVector(150.f, 0.f, HC * 0.25f), FVector(40.f, 230.f, HC * 0.5f), FRotator::ZeroRotator, Carpet);
	Box(FVector(170.f, 0.f, HC * 0.25f - 1.5f), FVector(2.f, 226.f, 3.f), FRotator::ZeroRotator, StripWhite, false);
	Box(FVector(150.f, 0.f, HC * 0.5f - 1.5f), FVector(2.f, 226.f, 3.f), FRotator::ZeroRotator, StripWhite, false);

	// ---- Handrail round the command platform: black posts, two grey rails, open at the front.
	{
		const float RR = RC + 8.f;
		const float Arc0 = 50.f, Arc1 = 310.f;
		for (float Z : { HC + 58.f, HC + 92.f })
		{
			Revolve({ FVector2D(RR - 2.5f, Z - 2.5f), FVector2D(RR + 2.5f, Z - 2.5f), FVector2D(RR + 2.5f, Z + 2.5f), FVector2D(RR - 2.5f, Z + 2.5f) }, Arc0, Arc1, RailGrey, false);
		}
		const int32 Posts = 14;
		for (int32 i = 0; i < Posts; ++i)
		{
			const float A = FMath::Lerp(Arc0, Arc1, (float)i / (Posts - 1));
			Cyl(FRotator(0.f, A, 0.f).Vector() * RR + FVector(0, 0, HC + 47.f), 2.2f, 94.f, ChairBlack, FRotator::ZeroRotator, false);
		}
		// Short rails flanking the stair.
		for (int32 s = -1; s <= 1; s += 2)
		{
			Box(FVector(150.f, s * 118.f, HC + 75.f), FVector(60.f, 5.f, 5.f), FRotator::ZeroRotator, RailGrey, false);
			Cyl(FVector(176.f, s * 118.f, HC * 0.5f + 40.f), 2.2f, 80.f, ChairBlack, FRotator::ZeroRotator, false);
		}
	}

	// ---- Walls: light-grey panels from the deck to the ceiling, a dark band behind the console banks, pale ceiling.
	Revolve({ FVector2D(R + 4.f, HD), FVector2D(R + 16.f, HD), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 4.f, H + 2.f) }, VS, 360.f - VS, PanelGrey);
	Revolve({ FVector2D(R + 1.f, HD + 84.f), FVector2D(R + 5.f, HD + 84.f), FVector2D(R + 5.f, HD + 150.f), FVector2D(R + 1.f, HD + 150.f) }, VS, 360.f - VS, PanelDark);
	Revolve({ FVector2D(R - 1.f, H - 30.f), FVector2D(R + 6.f, H - 30.f), FVector2D(R + 6.f, H - 26.f), FVector2D(R - 1.f, H - 26.f) }, VS, 360.f - VS, StripWhite, false);
	// Ceiling: pale grey disc, a ring of radial ribs, a lit central fixture.
	Cyl(FVector(0, 0, H + 6.f), R + 20.f, 12.f, PanelGrey);
	Revolve({ FVector2D(0.f, H - 1.f), FVector2D(R * 0.98f, H - 1.f), FVector2D(R * 0.98f, H + 1.f), FVector2D(0.f, H + 1.f) }, 0.f, 360.f, LightGrey);
	for (int32 i = 0; i < 12; ++i)
	{
		const float A = i * 30.f + 15.f;
		Box(FRotator(0.f, A, 0.f).Vector() * (R * 0.62f) + FVector(0, 0, H - 4.f), FVector(R * 0.62f, 8.f, 8.f), FRotator(0.f, A, 0.f), PanelDark, false);
	}
	Cyl(FVector(0, 0, H - 5.f), 150.f, 6.f, PanelDark);
	Revolve({ FVector2D(118.f, H - 12.f), FVector2D(146.f, H - 12.f), FVector2D(146.f, H - 8.f), FVector2D(118.f, H - 8.f) }, 0.f, 360.f, StripWhite, false);
	Cyl(FVector(0, 0, H - 10.f), 60.f, 4.f, StripWhite, FRotator::ZeroRotator, false);

	// ---- Perimeter stations: consoles every 32 degrees, pilasters between them, the turbolifts and viewscreen left clear.
	const float Step = 32.f;
	const float Chord = 2.f * R * FMath::Sin(FMath::DegreesToRadians(Step * 0.5f));
	float Seed = 11.f;
	TArray<float> StationAngles;
	for (float A = VS + Step * 0.5f + 4.f; A < 360.f - VS - Step * 0.4f; A += Step)
	{
		bool bLift = false;
		for (float L : Lifts)
		{
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(A, L)) < LiftHalf + Step * 0.45f)
			{
				bLift = true;
			}
		}
		if (bLift)
		{
			continue;
		}
		const bool bAft = FMath::Abs(FMath::FindDeltaAngleDegrees(A, 180.f)) < 8.f;
		RefitStation(A, Chord, Seed, !bAft);
		if (!bAft)
		{
			StationAngles.Add(A);
		}
	}
	// Pilasters: pale wedges between the stations and either side of the viewscreen.
	for (float A = VS + 4.f; A <= 360.f - VS - 3.f; A += Step)
	{
		bool bNearLift = false;
		for (float L : Lifts)
		{
			bNearLift |= FMath::Abs(FMath::FindDeltaAngleDegrees(A, L)) < LiftHalf + 8.f;
		}
		if (!bNearLift)
		{
			WallBox(A, R - 30.f, (HD + H) * 0.5f, 60.f, 14.f, H - HD, PanelGrey);
		}
	}

	// ---- Viewscreen: wide opening with a pale frame, a dark sill console beneath it.
	Revolve({ FVector2D(R + 2.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, ViewscreenBottom), FVector2D(R + 2.f, ViewscreenBottom) }, -VS, VS, PanelGrey);
	Revolve({ FVector2D(R - 4.f, ViewscreenBottom - 8.f), FVector2D(R + 8.f, ViewscreenBottom - 8.f), FVector2D(R + 8.f, ViewscreenBottom), FVector2D(R - 4.f, ViewscreenBottom) }, -VS, VS, LightGrey, false);
	Revolve({ FVector2D(R - 4.f, ViewscreenBottom - 11.f), FVector2D(R + 2.f, ViewscreenBottom - 11.f), FVector2D(R + 2.f, ViewscreenBottom - 8.f), FVector2D(R - 4.f, ViewscreenBottom - 8.f) }, -VS, VS, StripWhite, false);
	Revolve({ FVector2D(R + 2.f, ViewscreenTop), FVector2D(R + 16.f, ViewscreenTop), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 2.f, H + 2.f) }, -VS, VS, PanelGrey);
	Revolve({ FVector2D(R - 4.f, ViewscreenTop), FVector2D(R + 8.f, ViewscreenTop), FVector2D(R + 8.f, ViewscreenTop + 8.f), FVector2D(R - 4.f, ViewscreenTop + 8.f) }, -VS, VS, LightGrey, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		WallBox(s * (VS + 1.f), R + 2.f, (HD + H) * 0.5f, 30.f, 22.f, H - HD, LightGrey);
	}

	// ---- Turbolifts: two burgundy door pairs aft in pale surrounds, a lit sign over each; between them the
	// aft display wall; a burgundy bench along the starboard quarter.
	for (float A : Lifts)
	{
		WallBox(A, R + 8.f, HD + 110.f, 8.f, 150.f, 220.f, LightGrey);
		WallBox(A, R + 1.f, HD + 100.f, 4.f, 110.f, 200.f, Burgundy);
		WallBox(A, R - 1.5f, HD + 100.f, 1.f, 2.f, 196.f, ChairBlack, 0.f, false);
		WallBox(A, R - 1.f, HD + 212.f, 2.f, 60.f, 8.f, StripWhite, 0.f, false);
	}
	{
		// Bench under the aft monitor, between the two lifts.
		const float A = 180.f;
		WallBox(A, R - 30.f, HD + 22.f, 48.f, 170.f, 44.f, Burgundy);
		WallBox(A, R - 8.f, HD + 60.f, 10.f, 170.f, 40.f, Burgundy);
		WallBox(A, R - 54.f, HD + 42.f, 4.f, 166.f, 3.f, StripWhite, 0.f, false);
	}

	// ---- Helm and navigation: one curved pale console a step below the platform, dark top with teal panels, two chairs.
	{
		const float X0 = 240.f;
		Revolve({ FVector2D(X0 - 10.f, 0.f), FVector2D(X0 + 60.f, 0.f), FVector2D(X0 + 60.f, 78.f), FVector2D(X0 - 10.f, 86.f) }, -44.f, 44.f, PanelGrey);
		Revolve({ FVector2D(X0 - 12.f, 4.f), FVector2D(X0 - 8.f, 4.f), FVector2D(X0 - 8.f, 7.f), FVector2D(X0 - 12.f, 7.f) }, -44.f, 44.f, StripWhite, false);
		Revolve({ FVector2D(X0 - 6.f, 86.f), FVector2D(X0 + 56.f, 79.f), FVector2D(X0 + 56.f, 82.f), FVector2D(X0 - 6.f, 89.f) }, -44.f, 44.f, ScreenDark, false);
		for (int32 s = -1; s <= 1; s += 2)
		{
			UMaterialInstanceDynamic* Pan = MakeScreen(DisplayMaterial, s < 0 ? TEXT("RefitHelm") : TEXT("RefitNav"), 101.f + s, DisplayIntensity, 0.9f);
			if (Pan)
			{
				Pan->SetVectorParameterValue(TEXT("ColorA"), FLinearColor(0.25f, 0.95f, 0.8f));
				Pan->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(0.9f, 0.6f, 0.2f));
				Pan->SetVectorParameterValue(TEXT("ColorC"), FLinearColor(0.35f, 0.9f, 0.4f));
				Pan->SetVectorParameterValue(TEXT("ColorD"), FLinearColor(0.2f, 0.7f, 0.9f));
			}
			const float A = s * 24.f;
			const FVector Mid = FRotator(0.f, A, 0.f).Vector() * (X0 + 26.f) + FVector(0, 0, 87.5f);
			Box(Mid, FVector(50.f, 80.f, 1.5f), FRotator(-6.f, A, 0.f), Pan, false);
			RefitChair(FRotator(0.f, A, 0.f).Vector() * (X0 - 70.f), A, true, UniformMaroon);
		}
	}

	// ---- Captain's chair at the platform centre: grey high-backed seat on a black pedestal, black arms with
	// small touch panels; the camera sits at its headrest.
	Cyl(FVector(0, 0, HC + 2.5f), 70.f, 5.f, PanelDark);
	Cyl(FVector(0, 0, HC + 22.f), 12.f, 36.f, ChairBlack);
	Box(FVector(-6.f, 0, HC + 46.f), FVector(62, 64, 12), FRotator::ZeroRotator, SeatGrey);
	Box(FVector(-34.f, 0, HC + 50.f), FVector(12, 62, 68), FRotator(-8.f, 0.f, 0.f), SeatGrey);
	Box(FVector(-42.f, 0, HC + 50.f), FVector(4, 66, 72), FRotator(-8.f, 0.f, 0.f), ChairBlack);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(-6.f, s * 38.f, HC + 70.f), FVector(60, 10, 8), FRotator::ZeroRotator, ChairBlack);
		Box(FVector(10.f, s * 38.f, HC + 74.5f), FVector(20, 8, 1.5f), FRotator::ZeroRotator, MakeScreen(ButtonsMaterial, TEXT("RefitArm"), 200.f + s, DisplayIntensity * 0.7f, 0.5f), false);
	}

	// ---- Perimeter crew: chairs at the stations, some occupied.
	int32 k = 0;
	for (float A : StationAngles)
	{
		const bool bCrew = (k % 3) != 1;
		UMaterialInterface* Uni = (k % 2 == 0) ? UniformMaroon : UniformRed;
		RefitChair(FRotator(0.f, A, 0.f).Vector() * (R - 128.f) + FVector(0, 0, HD), A, bCrew, Uni);
		++k;
	}

	// ---- Lighting: cooler and dimmer than the original; the strips do the accenting.
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * 60.f + 30.f;
		Light(FRotator(0.f, A, 0.f).Vector() * (R * 0.55f) + FVector(0, 0, H - 110.f), LightIntensity * 0.8f, 1300.f);
	}
	Light(FVector(0, 0, H - 40.f), LightIntensity * 0.6f, 1200.f);
}
