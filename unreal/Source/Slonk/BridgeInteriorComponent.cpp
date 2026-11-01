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
	OkudagramMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Okudagram.M_Okudagram")));
	RefitMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Bridge/SM_BridgeRefit.SM_BridgeRefit")));
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
	TacticalScreens.Reset();
	HelmScreens.Reset();
	RefitStationIndex = 0;
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

	if (Style == EBridgeStyle::RefitMesh)
	{
		BuildRefitMesh();
	}
	else if (Style == EBridgeStyle::Refit)
	{
		BuildRefit();
	}
	else
	{
		BuildOriginal();
	}
}

void UBridgeInteriorComponent::BuildRefitMesh()
{
	UStaticMesh* Mesh = RefitMesh.LoadSynchronous();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Bridge: SM_BridgeRefit missing, building the primitive refit instead."));
		BuildRefit();
		return;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("BridgeMesh"));
	PrepPrimitive(C, true);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocationAndRotation(FVector::ZeroVector, RefitMeshRotation);
	C->RegisterComponent();
	Pieces.Add(C);

	// Bind slots by name: screens to the live Okudagram material, light strips and the clock to emissive instances;
	// everything else keeps the material the import made from the Blender colours.
	UMaterialInterface* Strip = MakeLit(TEXT("MeshStrip"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), LightPanelEmissive * 2.5f);
	UMaterialInterface* Clock = MakeLit(TEXT("MeshClock"), FLinearColor(0.1f, 0.0f, 0.0f), 0.5f, 0.f, FLinearColor(1.0f, 0.1f, 0.05f), 6.f);
	UMaterialInterface* Touch = MakeScreen(ButtonsMaterial, TEXT("MeshTouch"), 7.f, DisplayIntensity * 0.6f, 0.6f);
	const TArray<FName> Slots = C->GetMaterialSlotNames();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FString Name = Slots[i].ToString();
		if (Name.StartsWith(TEXT("Screen_")))
		{
			const FString Tex = Name.RightChop(7);
			if (UMaterialInstanceDynamic* Screen = MakeOkuda(*Tex, DisplayIntensity * 0.55f))
			{
				C->SetMaterial(i, Screen);
				if (Tex == TEXT("Tactical")) { TacticalScreens.Add(Screen); }
				if (Tex == TEXT("Helm")) { HelmScreens.Add(Screen); }
			}
		}
		else if (Name == TEXT("LightStrip") && Strip) { C->SetMaterial(i, Strip); }
		else if (Name == TEXT("LightSoft")) { C->SetMaterial(i, MakeLit(TEXT("MeshSoft"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), LightPanelEmissive * 0.8f)); }
		else if (Name == TEXT("ClockRed") && Clock) { C->SetMaterial(i, Clock); }
		else if (Name == TEXT("TouchPanel") && Touch) { C->SetMaterial(i, Touch); }
	}

	// Crew on the Blender set's chairs: the helm chairs on the platform (30 cm up) and a few of the station chairs
	// on the deck (60 cm up), 1.15 m in from the wall; the two desk stations aft of the lifts have their chair
	// shifted 30 cm aft of the station's bearing, away from the lift.
	if (bShowCrew)
	{
		UniformMaroon = MakeLit(TEXT("UniformMaroon"), FLinearColor(0.40f, 0.07f, 0.09f), 0.85f);
		for (int32 s = -1; s <= 1; s += 2)
		{
			Crew(FVector(30.f, s * 82.f, 30.f + 55.f), 0.f, UniformMaroon);
		}
		const float ChairR = WallRadius - 115.f, DeckZ = 60.f + 55.f;
		const float Seats[] = { 40.f, 68.f, -54.f, -82.f };
		int32 k = 0;
		for (float A : Seats)
		{
			Crew(FRotator(0.f, A, 0.f).Vector() * ChairR + FVector(0, 0, DeckZ), A, (k++ % 2) ? UniformMaroon : UniformRed);
		}
		for (float A : { 126.f, -126.f })
		{
			const float Aft = A > 0.f ? 1.f : -1.f;
			Crew(FRotator(0.f, A, 0.f).Vector() * ChairR + FRotator(0.f, A + 90.f, 0.f).Vector() * (30.f * Aft) + FVector(0, 0, DeckZ), A, (k++ % 2) ? UniformMaroon : UniformRed);
		}
	}

	// Lighting as for the primitive refit.
	const float R = WallRadius, H = CeilingHeight;
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * 60.f + 30.f;
		Light(FRotator(0.f, A, 0.f).Vector() * (R * 0.6f) + FVector(0, 0, H - 100.f), LightIntensity * 0.7f, 1300.f);
	}
	Light(FVector(0, 0, H - 50.f), LightIntensity * 0.5f, 1200.f);
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
// After Donny Versiga's renders of the 2293 bridge. Light-grey cabinets and pilasters, a dark upper wall and
// ceiling with square recessed light panels, white light bars everywhere (three stacked at every console's
// foot, vertical runs up the pilaster edges, under the deck lips), large raked black display banks with teal
// graphics, a black-railed command platform with a chevron stair down the front and lamp posts either side,
// the shallow-V helm/navigation console with its central schematic display, the tall quilted captain's chair
// flanked by two smaller chairs, burgundy turbolift doors aft with a burgundy bench under the aft stations,
// and a red digital clock above. Same frame as the original: origin under the captain's chair, +X forward.

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeOkuda(const TCHAR* TextureName, float Intensity)
{
	UMaterialInterface* Parent = OkudagramMaterial.LoadSynchronous();
	UTexture* Tex = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/Slonk/Bridge/T_Okuda_%s.T_Okuda_%s"), TextureName, TextureName));
	if (!Parent || !Tex)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_Okuda_%s_%d"), TextureName, PieceCount));
	M->SetTextureParameterValue(TEXT("Screen"), Tex);
	M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	M->SetScalarParameterValue(TEXT("UVMode"), ScreenUVMode);
	Materials.Add(M);
	return M;
}

void UBridgeInteriorComponent::SetLiveReadouts(float ShieldFraction, float HullFraction, float Throttle, float WarpFraction, bool bAlert)
{
	for (UMaterialInstanceDynamic* M : TacticalScreens)
	{
		if (M)
		{
			M->SetScalarParameterValue(TEXT("BarA"), FMath::Clamp(ShieldFraction, 0.f, 1.f));
			M->SetScalarParameterValue(TEXT("BarB"), FMath::Clamp(HullFraction, 0.f, 1.f));
			M->SetScalarParameterValue(TEXT("Flash"), bAlert ? 1.f : 0.f);
		}
	}
	for (UMaterialInstanceDynamic* M : HelmScreens)
	{
		if (M)
		{
			M->SetScalarParameterValue(TEXT("BarA"), FMath::Clamp(FMath::Abs(Throttle), 0.f, 1.f));
			M->SetScalarParameterValue(TEXT("BarB"), FMath::Clamp(WarpFraction, 0.f, 1.f));
		}
	}
}

void UBridgeInteriorComponent::SetAlertLighting(int32 Level, float Time)
{
	// Gather the point lights once (or again after a rebuild).
	int32 Count = 0;
	for (USceneComponent* P : Pieces) { if (Cast<UPointLightComponent>(P)) { ++Count; } }
	bool bStale = Count != AlertLights.Num();
	for (const FAlertLight& A : AlertLights) { if (bStale) { break; } UPointLightComponent* L = A.Light.Get(); bStale = !L || !Pieces.Contains(L); }
	if (bStale)
	{
		AlertLights.Reset();
		for (USceneComponent* P : Pieces) { if (UPointLightComponent* L = Cast<UPointLightComponent>(P)) { AlertLights.Add({ L, L->Intensity }); } }
	}
	if (Level == 0 && LastAlertLevel == 0) { return; }
	LastAlertLevel = Level;
	FLinearColor Col = LightColor; float Scale = 1.f;
	if (Level == 1) { Col = FLinearColor::LerpUsingHSV(LightColor, FLinearColor(1.f, 0.72f, 0.35f), 0.6f); Scale = 0.85f; }
	else if (Level == 2) { Col = FLinearColor(1.f, 0.22f, 0.16f); Scale = 0.55f + 0.3f * (0.5f + 0.5f * FMath::Sin(Time * 2.4f)); }
	for (const FAlertLight& A : AlertLights)
	{
		if (UPointLightComponent* L = A.Light.Get()) { L->SetLightColor(Col); L->SetIntensity(A.BaseIntensity * Scale); }
	}
}

UMaterialInstanceDynamic* UBridgeInteriorComponent::RefitScreen(const TCHAR* Name, float Seed, float Intensity, float CellScale, float Gap)
{
	UMaterialInstanceDynamic* M = MakeScreen(DisplayMaterial, Name, Seed, Intensity, CellScale);
	if (M)
	{
		M->SetScalarParameterValue(TEXT("Gap"), Gap);
		M->SetVectorParameterValue(TEXT("PanelColor"), FLinearColor(0.004f, 0.006f, 0.008f));
		M->SetVectorParameterValue(TEXT("ColorA"), FLinearColor(0.2f, 0.95f, 0.75f));
		M->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(0.15f, 0.6f, 1.0f));
		M->SetVectorParameterValue(TEXT("ColorC"), FLinearColor(0.3f, 0.9f, 0.35f));
		M->SetVectorParameterValue(TEXT("ColorD"), FLinearColor(0.6f, 0.95f, 0.9f));
	}
	return M;
}

void UBridgeInteriorComponent::LightBars(float AngleDeg, float Radial, float Z, float Width, int32 Count, float Pitch)
{
	for (int32 i = 0; i < Count; ++i)
	{
		WallBox(AngleDeg, Radial, Z + i * Pitch, 2.f, Width, 5.f, StripWhite, 0.f, false);
	}
}

void UBridgeInteriorComponent::RefitChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Round black base, black column, grey upholstered seat and an egg-shaped grey back on a black spine,
	// with black hoop arms.
	Cyl(FloorPos + FVector(0, 0, 3.f), 27.f, 6.f, ChairBlack, R);
	Cyl(FloorPos + FVector(0, 0, 26), 6.f, 46.f, ChairBlack, R);
	Box(FloorPos + FVector(0, 0, 50), FVector(46, 46, 9), R, SeatGrey);
	Box(FloorPos - F * 20.f + FVector(0, 0, 62), FVector(6, 14, 30), R, ChairBlack);
	Piece(SphereMesh, FloorPos - F * 23.f + FVector(0, 0, 90), FVector(12, 44, 64), R, SeatGrey);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FloorPos - F * 4.f + FVector(0, 0, 72) + R.RotateVector(FVector(0, s * 25.f, 0)), FVector(34, 3.5f, 3.5f), R, ChairBlack);
		Box(FloorPos - F * 20.f + FVector(0, 0, 62) + R.RotateVector(FVector(0, s * 25.f, 0)), FVector(3.5f, 3.5f, 22), R, ChairBlack);
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
		// Cabinet: light-grey block with three light bars at its foot, a black desk top with lit touch panels,
		// and the big raked display bank leaning back against the wall above a black bezel.
		WallBox(A, R - 40.f, HD + 40.f, 60.f, Chord - 12.f, 80.f, PanelGrey);
		LightBars(A, R - 71.f, HD + 8.f, Chord - 24.f);
		WallBox(A, R - 48.f, HD + 81.5f, 48.f, Chord - 14.f, 3.f, ScreenDark, 0.f, false);
		WallBox(A, R - 52.f, HD + 83.6f, 36.f, Chord - 30.f, 1.5f, MakeScreen(ButtonsMaterial, TEXT("RefitTouch"), Seed, DisplayIntensity * 0.8f, 0.7f), 0.f, false);
		WallBox(A, R - 12.f, HD + 132.f, 10.f, Chord - 10.f, 108.f, ScreenDark, -16.f, false);
		// Okudagram screens, dealt round the ring from the starboard bow: weapons, tactical (live), communications,
		// then science, engineering, environmental, navigation on the port side.
		static const TCHAR* Order[] = { TEXT("Weapons"), TEXT("Tactical"), TEXT("Communications"), TEXT("Science"), TEXT("Engineering"), TEXT("Environmental"), TEXT("Navigation") };
		const TCHAR* TexName = Order[RefitStationIndex % UE_ARRAY_COUNT(Order)];
		++RefitStationIndex;
		UMaterialInstanceDynamic* Screen = MakeOkuda(TexName, DisplayIntensity * 0.55f);
		if (Screen && FCString::Strcmp(TexName, TEXT("Tactical")) == 0)
		{
			TacticalScreens.Add(Screen);
		}
		UMaterialInterface* BankMat = Screen ? static_cast<UMaterialInterface*>(Screen) : RefitScreen(TEXT("RefitBank"), Seed + 0.5f, DisplayIntensity * 0.7f, 2.4f, 0.6f);
		// The bank is twice as wide as tall, like the textures.
		WallBox(A, R - 18.4f, HD + 132.f, 2.f, Chord - 22.f, (Chord - 22.f) * 0.5f, BankMat, -16.f, false);
	}
	else
	{
		// The master situation display alone over the aft bench.
		WallBox(A, R - 4.f, HD + 150.f, 8.f, Chord - 10.f, 104.f, ScreenDark, -6.f, false);
		UMaterialInstanceDynamic* Screen = MakeOkuda(TEXT("MasterSituation"), DisplayIntensity * 0.55f);
		UMaterialInterface* BankMat = Screen ? static_cast<UMaterialInterface*>(Screen) : RefitScreen(TEXT("RefitAftBank"), Seed + 0.5f, DisplayIntensity * 0.7f, 2.4f, 0.6f);
		WallBox(A, R - 8.6f, HD + 150.f, 2.f, Chord - 22.f, (Chord - 22.f) * 0.5f, BankMat, -6.f, false);
	}
	Seed += 7.f;
}

void UBridgeInteriorComponent::BuildRefit()
{
	PanelGrey = MakeLit(TEXT("PanelGrey"), FLinearColor(0.52f, 0.53f, 0.55f), 0.6f);
	PanelDark = MakeLit(TEXT("PanelDark"), FLinearColor(0.14f, 0.15f, 0.165f), 0.75f);
	Carpet = MakeLit(TEXT("Carpet"), FLinearColor(0.11f, 0.115f, 0.12f), 0.9f);
	StripWhite = MakeLit(TEXT("StripWhite"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), LightPanelEmissive * 2.5f);
	Burgundy = MakeLit(TEXT("Burgundy"), FLinearColor(0.28f, 0.045f, 0.055f), 0.7f);
	SeatGrey = MakeLit(TEXT("SeatGrey"), FLinearColor(0.58f, 0.58f, 0.59f), 0.95f);
	RailGrey = MakeLit(TEXT("RailGrey"), FLinearColor(0.45f, 0.45f, 0.47f), 0.35f, 0.4f);
	ScreenDark = MakeLit(TEXT("ScreenDark"), FLinearColor(0.012f, 0.014f, 0.018f), 0.3f);
	UniformMaroon = MakeLit(TEXT("UniformMaroon"), FLinearColor(0.40f, 0.07f, 0.09f), 0.85f);
	ClockRed = MakeLit(TEXT("ClockRed"), FLinearColor(0.1f, 0.0f, 0.0f), 0.5f, 0.f, FLinearColor(1.0f, 0.1f, 0.05f), 6.f);

	const float R = WallRadius;
	const float HD = DeckHeight;          // perimeter deck above the helm floor
	const float HC = HD + 26.f;           // command platform
	const float H = CeilingHeight;
	const float VS = ViewscreenHalfAngle;
	const float RC = 215.f;               // command platform radius
	const float RD = DeckRadius;          // inner edge of the perimeter deck
	const float Lifts[2] = { 150.f, 210.f };
	const float LiftHalf = 12.f;

	// ---- Floors. Helm floor, perimeter deck ring with a black riser and a light strip under its lip, the two-tier
	// command platform behind the helm with black risers, and a chevron stair down the front.
	Cyl(FVector(0, 0, -3), RD + 10.f, 6.f, Carpet);
	Revolve({ FVector2D(RD, 0), FVector2D(R + 15.f, 0), FVector2D(R + 15.f, HD), FVector2D(RD, HD) }, 0.f, 360.f, Carpet);
	Revolve({ FVector2D(RD - 1.5f, 0.f), FVector2D(RD + 1.f, 0.f), FVector2D(RD + 1.f, HD - 5.f), FVector2D(RD - 1.5f, HD - 5.f) }, 0.f, 360.f, ChairBlack);
	Revolve({ FVector2D(RD - 1.f, HD - 5.f), FVector2D(RD + 3.f, HD - 5.f), FVector2D(RD + 3.f, HD - 1.f), FVector2D(RD - 1.f, HD - 1.f) }, 0.f, 360.f, StripWhite, false);
	Revolve({ FVector2D(0.f, 0.f), FVector2D(RC, 0.f), FVector2D(RC, HC), FVector2D(0.f, HC) }, 46.f, 314.f, Carpet);
	Revolve({ FVector2D(RC - 1.f, 0.f), FVector2D(RC + 1.5f, 0.f), FVector2D(RC + 1.5f, HC - 5.f), FVector2D(RC - 1.f, HC - 5.f) }, 46.f, 314.f, ChairBlack);
	Revolve({ FVector2D(RC - 1.f, HC - 5.f), FVector2D(RC + 2.5f, HC - 5.f), FVector2D(RC + 2.5f, HC - 1.f), FVector2D(RC - 1.f, HC - 1.f) }, 46.f, 314.f, StripWhite, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		// Squared shoulders either side of the stair, black-faced.
		Box(FVector(100.f, s * 170.f, HC * 0.5f), FVector(96.f, 112.f, HC), FRotator::ZeroRotator, Carpet);
		Box(FVector(149.f, s * 170.f, HC * 0.5f - 2.5f), FVector(2.f, 112.f, HC - 5.f), FRotator::ZeroRotator, ChairBlack);
		Box(FVector(149.5f, s * 170.f, HC - 3.f), FVector(2.f, 110.f, 4.f), FRotator::ZeroRotator, StripWhite, false);
	}
	// Two steps, chevron-pointed toward the helm.
	for (int32 st = 0; st < 2; ++st)
	{
		const float Top = HC * (st == 0 ? 0.5f : 1.0f);
		const float X = st == 0 ? 160.f : 136.f;
		Box(FVector(X, 0.f, Top * 0.5f), FVector(28.f, 232.f - st * 20.f, Top), FRotator::ZeroRotator, Carpet);
		for (int32 s = -1; s <= 1; s += 2)
		{
			Box(FVector(X + 12.f, s * 50.f, Top * 0.5f), FVector(28.f, 100.f, Top), FRotator(0.f, s * -22.f, 0.f), Carpet);
		}
		Box(FVector(X + 26.f, 0.f, Top - 2.5f), FVector(2.f, 60.f, 4.f), FRotator::ZeroRotator, StripWhite, false);
	}

	// ---- Handrail round the command platform: black round top rail and a thin lower rail on flat grey posts,
	// open at the front; a black lamp post with light bars at each stair end.
	{
		const float RR = RC + 10.f;
		const float Arc0 = 50.f, Arc1 = 310.f;
		Revolve({ FVector2D(RR - 3.f, HC + 93.f), FVector2D(RR + 3.f, HC + 93.f), FVector2D(RR + 3.f, HC + 99.f), FVector2D(RR - 3.f, HC + 99.f) }, Arc0, Arc1, ChairBlack, false);
		Revolve({ FVector2D(RR - 1.5f, HC + 52.f), FVector2D(RR + 1.5f, HC + 52.f), FVector2D(RR + 1.5f, HC + 55.f), FVector2D(RR - 1.5f, HC + 55.f) }, Arc0, Arc1, RailGrey, false);
		const int32 Posts = 16;
		for (int32 i = 0; i < Posts; ++i)
		{
			const float A = FMath::Lerp(Arc0, Arc1, (float)i / (Posts - 1));
			Box(FRotator(0.f, A, 0.f).Vector() * RR + FVector(0, 0, HC + 47.f), FVector(2.f, 7.f, 94.f), FRotator(0.f, A, 0.f), RailGrey, false);
		}
		for (int32 s = -1; s <= 1; s += 2)
		{
			const FVector P(176.f, s * 132.f, 0.f);
			Cyl(P + FVector(0, 0, HC * 0.5f + 60.f), 7.f, 120.f, ChairBlack, FRotator::ZeroRotator, false);
			for (int32 k = 0; k < 3; ++k)
			{
				Cyl(P + FVector(0, 0, HC * 0.5f + 70.f + k * 14.f), 7.6f, 5.f, StripWhite, FRotator::ZeroRotator, false);
			}
			// Small black side table beside each post.
			Cyl(P + FVector(-30.f, s * 30.f, HC + 55.f), 4.f, 60.f, ChairBlack, FRotator::ZeroRotator, false);
			Piece(CylinderMesh, P + FVector(-30.f, s * 30.f, HC + 86.f), FVector(70.f, 34.f, 4.f), FRotator(0.f, s * 30.f, 0.f), ChairBlack, false);
		}
	}

	// ---- Walls and ceiling: light-grey wall behind the cabinets, dark grey above the display banks and overhead,
	// square recessed light panels in a ring, a ribbed grey dome at the centre.
	Revolve({ FVector2D(R + 4.f, HD), FVector2D(R + 16.f, HD), FVector2D(R + 16.f, HD + 150.f), FVector2D(R + 4.f, HD + 150.f) }, VS, 360.f - VS, PanelGrey);
	Revolve({ FVector2D(R + 4.f, HD + 150.f), FVector2D(R + 16.f, HD + 150.f), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 4.f, H + 2.f) }, VS, 360.f - VS, PanelDark);
	Revolve({ FVector2D(R - 2.f, H - 36.f), FVector2D(R + 6.f, H - 36.f), FVector2D(R + 6.f, H - 32.f), FVector2D(R - 2.f, H - 32.f) }, VS, 360.f - VS, StripWhite, false);
	Cyl(FVector(0, 0, H + 6.f), R + 20.f, 12.f, PanelDark);
	Revolve({ FVector2D(0.f, H - 1.f), FVector2D(R * 0.98f, H - 1.f), FVector2D(R * 0.98f, H + 1.f), FVector2D(0.f, H + 1.f) }, 0.f, 360.f, PanelDark);
	for (int32 i = 0; i < 10; ++i)
	{
		const float A = i * 36.f + 18.f;
		Box(FRotator(0.f, A, 0.f).Vector() * (R * 0.72f) + FVector(0, 0, H - 2.f), FVector(70.f, 44.f, 3.f), FRotator(0.f, A, 0.f), StripWhite, false);
	}
	Piece(SphereMesh, FVector(0, 0, H + 20.f), FVector(240.f, 240.f, 90.f), FRotator::ZeroRotator, PanelGrey, false);
	Revolve({ FVector2D(90.f, H - 22.f), FVector2D(122.f, H - 22.f), FVector2D(122.f, H - 18.f), FVector2D(90.f, H - 18.f) }, 0.f, 360.f, StripWhite, false);
	Cyl(FVector(0, 0, H - 26.f), 52.f, 4.f, StripWhite, FRotator::ZeroRotator, false);

	// ---- Perimeter stations every 32 degrees, pilasters with vertical light bars between them; the turbolifts
	// and the viewscreen are left clear.
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
	for (float A = VS + 4.f; A <= 360.f - VS - 3.f; A += Step)
	{
		bool bNearLift = false;
		for (float L : Lifts)
		{
			bNearLift |= FMath::Abs(FMath::FindDeltaAngleDegrees(A, L)) < LiftHalf + 8.f;
		}
		if (bNearLift)
		{
			continue;
		}
		WallBox(A, R - 30.f, (HD + H) * 0.5f, 64.f, 16.f, H - HD, PanelGrey);
		// A vertical light bar up each edge of the pilaster's inner face.
		for (int32 s = -1; s <= 1; s += 2)
		{
			const FVector Dir = FRotator(0.f, A, 0.f).Vector();
			const FVector Side = FRotator(0.f, A + 90.f, 0.f).Vector();
			Box(Dir * (R - 63.f) + Side * (s * 6.5f) + FVector(0, 0, HD + 90.f + (H - HD - 110.f) * 0.5f), FVector(2.f, 2.5f, H - HD - 110.f), FRotator(0.f, A, 0.f), StripWhite, false);
		}
	}

	// ---- Viewscreen: a wide opening in a light-grey frame with a sill console under it and a dark lintel.
	Revolve({ FVector2D(R + 2.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, ViewscreenBottom), FVector2D(R + 2.f, ViewscreenBottom) }, -VS, VS, PanelGrey);
	LightBars(0.f, R - 1.f, HD + 8.f, 200.f);
	Revolve({ FVector2D(R - 6.f, ViewscreenBottom - 10.f), FVector2D(R + 8.f, ViewscreenBottom - 10.f), FVector2D(R + 8.f, ViewscreenBottom), FVector2D(R - 6.f, ViewscreenBottom) }, -VS, VS, LightGrey, false);
	Revolve({ FVector2D(R + 2.f, ViewscreenTop), FVector2D(R + 16.f, ViewscreenTop), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 2.f, H + 2.f) }, -VS, VS, PanelDark);
	Revolve({ FVector2D(R - 6.f, ViewscreenTop), FVector2D(R + 8.f, ViewscreenTop), FVector2D(R + 8.f, ViewscreenTop + 10.f), FVector2D(R - 6.f, ViewscreenTop + 10.f) }, -VS, VS, LightGrey, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		WallBox(s * (VS + 1.5f), R + 1.f, (HD + H) * 0.5f, 34.f, 24.f, H - HD, LightGrey);
		WallBox(s * (VS - 0.8f), R - 2.f, (ViewscreenBottom + ViewscreenTop) * 0.5f, 2.f, 2.5f, ViewscreenTop - ViewscreenBottom, StripWhite, 0.f, false);
	}

	// ---- Turbolifts aft: burgundy doors in pale surrounds with a small lit sign, light bars up the surround edges.
	for (float A : Lifts)
	{
		WallBox(A, R + 8.f, HD + 110.f, 8.f, 150.f, 220.f, LightGrey);
		WallBox(A, R + 1.f, HD + 100.f, 4.f, 110.f, 200.f, Burgundy);
		WallBox(A, R - 1.5f, HD + 100.f, 1.f, 2.f, 196.f, ChairBlack, 0.f, false);
		WallBox(A, R - 1.f, HD + 212.f, 2.f, 40.f, 7.f, ClockRed, 0.f, false);
		for (int32 s = -1; s <= 1; s += 2)
		{
			WallBox(A + s * 7.6f, R + 3.f, HD + 110.f, 2.f, 3.f, 216.f, StripWhite, 0.f, false);
		}
	}
	// The burgundy bench under the aft display, between the lifts, and the red clock above the aft centre.
	{
		const float A = 180.f;
		WallBox(A, R - 32.f, HD + 22.f, 52.f, 300.f, 44.f, Burgundy);
		WallBox(A, R - 10.f, HD + 62.f, 14.f, 300.f, 44.f, Burgundy);
		LightBars(A, R - 59.f, HD + 8.f, 296.f);
		WallBox(A, R + 2.f, H - 48.f, 2.f, 36.f, 9.f, ClockRed, 0.f, false);
	}

	// ---- Helm and navigation: a shallow-V console of two light-grey wings with black tops outlined in white,
	// slanted ends with three diagonal light bars, a grey base with a lit panel, and between the two chairs a
	// pedestal carrying the round ship schematic that faces the captain.
	{
		const float X0 = 236.f;
		for (int32 s = -1; s <= 1; s += 2)
		{
			const FRotator Rw(0.f, s * 20.f, 0.f);
			const FVector C = FVector(X0, s * 78.f, 0.f);
			Box(C + FVector(0, 0, 44.f), FVector(62.f, 168.f, 88.f), Rw, PanelGrey);
			Box(C + FVector(0, 0, 90.f), FVector(70.f, 174.f, 4.f), FRotator(-8.f, s * 20.f, 0.f), ScreenDark);
			Box(C + FVector(0, 0, 92.5f), FVector(72.f, 176.f, 1.2f), FRotator(-8.f, s * 20.f, 0.f), StripWhite, false);
			Box(C + FVector(1.f, 0, 93.2f), FVector(62.f, 166.f, 1.2f), FRotator(-8.f, s * 20.f, 0.f), ScreenDark, false);
			{
				UMaterialInstanceDynamic* Wing = MakeOkuda(s < 0 ? TEXT("Helm") : TEXT("Navigation"), DisplayIntensity * 0.5f);
				if (Wing && s < 0)
				{
					HelmScreens.Add(Wing);
				}
				UMaterialInterface* WingMat = Wing ? static_cast<UMaterialInterface*>(Wing) : MakeScreen(ButtonsMaterial, s < 0 ? TEXT("RefitHelm") : TEXT("RefitNav"), 101.f + s, DisplayIntensity * 0.9f, 0.7f);
				Box(C + FVector(4.f, 0, 94.2f), FVector(60.f, 120.f, 1.f), FRotator(-8.f, s * 20.f, 0.f), WingMat, false);
			}
			// Three diagonal light bars on the outboard end face.
			for (int32 k = 0; k < 3; ++k)
			{
				Box(C + Rw.RotateVector(FVector(-10.f + k * 12.f, s * 85.f, 0.f)) + FVector(0, 0, 40.f + k * 10.f), FVector(4.f, 2.f, 26.f), FRotator(0.f, s * 20.f, s * 35.f), StripWhite, false);
			}
		}
		Box(FVector(X0 - 20.f, 0.f, 44.f), FVector(60.f, 60.f, 88.f), FRotator::ZeroRotator, PanelGrey);
		Box(FVector(X0 - 51.f, 0.f, 40.f), FVector(2.f, 40.f, 48.f), FRotator::ZeroRotator, StripWhite, false);
		// Schematic pedestal between the helm chairs: tall enough that its screen clears the platform edge from
		// the captain's chair, with a small touch panel above the screen.
		Box(FVector(X0 - 92.f, 0.f, 70.f), FVector(28.f, 46.f, 140.f), FRotator::ZeroRotator, PanelGrey);
		Box(FVector(X0 - 106.5f, 0.f, 92.f), FVector(1.5f, 40.f, 84.f), FRotator::ZeroRotator, ScreenDark, false);
		{
			UMaterialInstanceDynamic* Sch = MakeOkuda(TEXT("Schematic"), DisplayIntensity * 0.6f);
			UMaterialInterface* SchMat = Sch ? static_cast<UMaterialInterface*>(Sch) : RefitScreen(TEXT("RefitSchematic"), 77.f, DisplayIntensity, 1.4f, 0.3f);
			Box(FVector(X0 - 107.5f, 0.f, 84.f), FVector(1.f, 34.f, 34.f), FRotator::ZeroRotator, SchMat, false);
			Box(FVector(X0 - 107.5f, 0.f, 118.f), FVector(1.f, 30.f, 10.f), FRotator::ZeroRotator, MakeScreen(ButtonsMaterial, TEXT("RefitPedestal"), 303.f, DisplayIntensity * 0.6f, 0.5f), false);
		}
		for (int32 s = -1; s <= 1; s += 2)
		{
			RefitChair(FVector(X0 - 92.f, s * 70.f, 0.f), 0.f, true, UniformMaroon);
		}
	}

	// ---- Captain's chair at the platform centre: tall quilted grey back with a headrest, black arms with small
	// touch panels, a grey pedestal; the camera sits at its headrest. Two smaller grey chairs flank it.
	Cyl(FVector(0, 0, HC + 2.5f), 60.f, 5.f, ChairBlack);
	Cyl(FVector(0, 0, HC + 22.f), 14.f, 36.f, PanelGrey);
	Box(FVector(-4.f, 0, HC + 46.f), FVector(60, 58, 12), FRotator::ZeroRotator, SeatGrey);
	// The back stays below the eye line (z 165 at the headrest) so the view aft is not our own chair.
	for (int32 k = 0; k < 4; ++k)
	{
		Box(FVector(-33.f - k * 1.0f, 0, HC + 52.f + k * 11.f), FVector(12, 54 - k * 2.f, 10), FRotator(-6.f, 0.f, 0.f), SeatGrey);
	}
	Box(FVector(-41.f, 0, HC + 58.f), FVector(4, 58, 76), FRotator(-6.f, 0.f, 0.f), ChairBlack);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(-4.f, s * 36.f, HC + 70.f), FVector(56, 10, 9), FRotator::ZeroRotator, ChairBlack);
		Box(FVector(12.f, s * 36.f, HC + 75.f), FVector(18, 8, 1.5f), FRotator::ZeroRotator, MakeScreen(ButtonsMaterial, TEXT("RefitArm"), 200.f + s, DisplayIntensity * 0.7f, 0.5f), false);
		RefitChair(FVector(-20.f, s * 128.f, HC), 0.f, false, nullptr);
	}

	// ---- Perimeter crew: chairs at the stations, some occupied.
	int32 k = 0;
	for (float A : StationAngles)
	{
		const bool bCrew = (k % 3) != 1;
		UMaterialInterface* Uni = (k % 2 == 0) ? UniformMaroon : UniformRed;
		RefitChair(FRotator(0.f, A, 0.f).Vector() * (R - 132.f) + FVector(0, 0, HD), A, bCrew, Uni);
		++k;
	}

	// ---- Lighting: cool, fairly dim fill; the strips and panels do the accenting.
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * 60.f + 30.f;
		Light(FRotator(0.f, A, 0.f).Vector() * (R * 0.6f) + FVector(0, 0, H - 100.f), LightIntensity * 0.7f, 1300.f);
	}
	Light(FVector(0, 0, H - 50.f), LightIntensity * 0.5f, 1200.f);
}
