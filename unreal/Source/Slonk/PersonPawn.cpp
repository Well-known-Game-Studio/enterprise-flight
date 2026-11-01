#include "PersonPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshPrims.h"
#include "PlanetBody.h"
#include "RcsThrusterComponent.h"
#include "SlonkMaterials.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "UDynamicMesh.h"

namespace
{
	constexpr ECollisionChannel BootsChannel = ECC_GameTraceChannel1;

	UDynamicMeshComponent* MakePart(AActor* Owner, const TCHAR* Name, USceneComponent* Parent, const FVector& Pivot)
	{
		UDynamicMeshComponent* C = Owner->CreateDefaultSubobject<UDynamicMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetRelativeLocation(Pivot);
		C->SetMobility(EComponentMobility::Movable);
		C->SetGenerateOverlapEvents(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->CastShadow = true;
		C->bCastDynamicShadow = true;
		C->bAffectDistanceFieldLighting = false;
		return C;
	}

	void FinishPart(UDynamicMeshComponent* C, UDynamicMesh* Scratch)
	{
		if (!C || !Scratch) { return; }
		FGeometryScriptCalculateNormalsOptions NO;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Scratch, NO);
		C->GetDynamicMesh()->SetMesh(MoveTemp(Scratch->GetMeshRef()));
		C->NotifyMeshUpdated();
	}

	FQuat StandingQuat(const FVector& Up, const FVector& ForwardHint)
	{
		FVector Fwd = ForwardHint - Up * FVector::DotProduct(ForwardHint, Up);
		if (Fwd.SizeSquared() < 1e-6)
		{
			Fwd = FVector::CrossProduct(Up, FVector::RightVector);
			if (Fwd.SizeSquared() < 1e-6) { Fwd = FVector::CrossProduct(Up, FVector::ForwardVector); }
		}
		return FRotationMatrix::MakeFromZX(Up, Fwd.GetSafeNormal()).ToQuat();
	}

	FVector MoveToward(const FVector& From, const FVector& To, double MaxStep)
	{
		const FVector D = To - From;
		const double L = D.Size();
		return L <= MaxStep || L < 1e-6 ? To : From + D * (MaxStep / L);
	}
}

APersonPawn::APersonPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the craft have moved and the floating origin has shifted the world (TG_DuringPhysics), so a person
	// standing on a hull is re-derived from the hull's final transform for this frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Body = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitCapsuleSize(28.f, HalfHeightCm);
	// The pawn does its own floor finding; nothing else ever needs to hit it.
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_Pawn);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);

	Torso = MakePart(this, TEXT("Torso"), Body, FVector::ZeroVector);
	Visor = MakePart(this, TEXT("Visor"), Body, FVector::ZeroVector);
	LeftArm = MakePart(this, TEXT("LeftArm"), Body, FVector(0.f, -24.f, 50.f));
	RightArm = MakePart(this, TEXT("RightArm"), Body, FVector(0.f, 24.f, 50.f));
	LeftLeg = MakePart(this, TEXT("LeftLeg"), Body, FVector(0.f, -10.f, 2.f));
	RightLeg = MakePart(this, TEXT("RightLeg"), Body, FVector(0.f, 10.f, 2.f));
	Visor->CastShadow = false;

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Body);
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	CameraArm->TargetArmLength = ThirdPersonDistanceCm;
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bInheritPitch = true;
	CameraArm->bInheritYaw = true;
	CameraArm->bInheritRoll = true;
	CameraArm->bEnableCameraLag = false;
	CameraArm->bEnableCameraRotationLag = true;
	CameraArm->CameraRotationLagSpeed = 12.f;
	// The hulls block the Boots channel, so the arm shortens against a saucer; the ground is handled by hand.
	CameraArm->bDoCollisionTest = true;
	CameraArm->ProbeChannel = BootsChannel;
	CameraArm->ProbeSize = 14.f;

	ThirdCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdCamera"));
	ThirdCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	ThirdCamera->bUsePawnControlRotation = false;
	ThirdCamera->SetFieldOfView(ThirdPersonFOV);

	HelmetCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HelmetCamera"));
	HelmetCamera->SetupAttachment(Body);
	HelmetCamera->SetRelativeLocation(FVector(10.f, 0.f, EyeHeightCm));
	HelmetCamera->bUsePawnControlRotation = false;
	HelmetCamera->SetFieldOfView(HelmetFOV);
	HelmetCamera->SetActive(false);

	Jets = CreateDefaultSubobject<URcsThrusterComponent>(TEXT("Jets"));
	Jets->SetupAttachment(Body);
	Jets->PlumeLength = 45.f;
	Jets->PlumeWidth = 14.f;
	Jets->LightCandela = 12.f;
	Jets->LightRadius = 400.f;
}

void APersonPawn::BeginPlay()
{
	Super::BeginPlay();
	BuildFigure();
	if (Jets)
	{
		// The pack: pairs of nozzles on the backpack for every axis, about the root.
		TArray<FRcsNozzle> N;
		for (float S : { -1.f, 1.f })
		{
			N.Add(FRcsNozzle(FVector(-30.f, S * 12.f, 30.f), FVector::BackwardVector, 0.5f));   // pushes forward
			N.Add(FRcsNozzle(FVector(-12.f, S * 17.f, 30.f), FVector::ForwardVector, 0.5f));    // pushes back
			N.Add(FRcsNozzle(FVector(-20.f, S * 17.f, 30.f), FVector(0.f, S, 0.f), 0.5f));      // pushes sideways
			N.Add(FRcsNozzle(FVector(-20.f, S * 12.f, 6.f), FVector::DownVector, 0.5f));        // lifts
			N.Add(FRcsNozzle(FVector(-20.f, S * 12.f, 54.f), FVector::UpVector, 0.5f));         // lowers
		}
		Jets->SetNozzles(N);
	}
	if (CameraArm) { CameraArm->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick); }
	SetCameraMode(CameraMode);
}

void APersonPawn::BuildFigure()
{
	using namespace SlonkMesh;
	UDynamicMesh* TM = NewObject<UDynamicMesh>(this);
	UDynamicMesh* VM = NewObject<UDynamicMesh>(this);
	UDynamicMesh* AL = NewObject<UDynamicMesh>(this);
	UDynamicMesh* AR = NewObject<UDynamicMesh>(this);
	UDynamicMesh* LL = NewObject<UDynamicMesh>(this);
	UDynamicMesh* LR = NewObject<UDynamicMesh>(this);

	// Root space: +X forward, +Z up, the root 90 cm above the feet. Suit (material 0), trim (1), boot lights (2).
	CurrentMaterialID() = 0;
	Box(TM, FVector(0.f, 0.f, 28.f), FVector(24.f, 36.f, 56.f));              // torso
	Ellipsoid(TM, FVector(0.f, 0.f, 72.f), FVector(16.f, 16.f, 17.f), 16);     // helmet
	for (float S : { -1.f, 1.f }) { Ellipsoid(TM, FVector(0.f, S * 22.f, 52.f), FVector(9.f, 9.f, 7.f), 10); } // shoulders
	CurrentMaterialID() = 1;
	CylinderZ(TM, FVector(0.f, 0.f, 55.f), 9.f, 5.f, 16);                     // neck ring
	Box(TM, FVector(-20.f, 0.f, 30.f), FVector(16.f, 30.f, 44.f));            // pack
	Box(TM, FVector(-29.f, 0.f, 30.f), FVector(3.f, 22.f, 30.f));             // pack plate
	Box(TM, FVector(13.f, 0.f, 40.f), FVector(3.f, 16.f, 10.f));              // chest panel
	for (float S : { -1.f, 1.f }) { Box(TM, FVector(-14.f, S * 17.f, 30.f), FVector(10.f, 4.f, 44.f)); } // pack straps

	CurrentMaterialID() = 0;
	Ellipsoid(VM, FVector(8.f, 0.f, 72.f), FVector(10.f, 12.f, 9.f), 14);     // visor

	for (UDynamicMesh* A : { AL, AR })
	{
		CurrentMaterialID() = 0;
		CylinderZ(A, FVector(0.f, 0.f, -56.f), 6.f, 56.f, 12);
		CurrentMaterialID() = 1;
		Ellipsoid(A, FVector(0.f, 0.f, -58.f), FVector(7.f, 7.f, 7.f), 10);  // glove
	}
	for (UDynamicMesh* L : { LL, LR })
	{
		CurrentMaterialID() = 0;
		CylinderZ(L, FVector(0.f, 0.f, -82.f), 8.f, 84.f, 12);
		CurrentMaterialID() = 1;
		Box(L, FVector(5.f, 0.f, -86.f), FVector(24.f, 13.f, 11.f));         // boot
		CurrentMaterialID() = 2;
		Box(L, FVector(5.f, 0.f, -91.f), FVector(25.f, 14.f, 2.f));          // boot light
	}

	FinishPart(Torso, TM);
	FinishPart(Visor, VM);
	FinishPart(LeftArm, AL);
	FinishPart(RightArm, AR);
	FinishPart(LeftLeg, LL);
	FinishPart(RightLeg, LR);

	if (UMaterialInterface* Hull = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::HullPath)), TEXT("Hull"), true))
	{
		auto Make = [&](const FLinearColor& C, float Rough)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Hull, this);
			M->SetVectorParameterValue(TEXT("HullColor"), C);
			M->SetScalarParameterValue(TEXT("PanelScale"), 3.f);
			M->SetScalarParameterValue(TEXT("Roughness"), Rough);
			M->SetScalarParameterValue(TEXT("Metallic"), 0.f);
			return M;
		};
		SuitMID = Make(FLinearColor(0.88f, 0.88f, 0.9f), 0.75f);
		TrimMID = Make(FLinearColor(0.22f, 0.23f, 0.27f), 0.5f);
	}
	if (UMaterialInterface* Glow = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("Glow"), false))
	{
		VisorMID = UMaterialInstanceDynamic::Create(Glow, this);
		VisorMID->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(1.f, 0.82f, 0.45f));
		VisorMID->SetScalarParameterValue(TEXT("Intensity"), 1.2f);
		BootLightMID = UMaterialInstanceDynamic::Create(Glow, this);
		BootLightMID->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.3f, 0.8f, 1.f));
		BootLightMID->SetScalarParameterValue(TEXT("Intensity"), 0.f);
	}
	if (SuitMID && TrimMID)
	{
		Torso->ConfigureMaterialSet({ SuitMID.Get(), TrimMID.Get() });
		LeftArm->ConfigureMaterialSet({ SuitMID.Get(), TrimMID.Get() });
		RightArm->ConfigureMaterialSet({ SuitMID.Get(), TrimMID.Get() });
		UMaterialInterface* BootLight = BootLightMID ? static_cast<UMaterialInterface*>(BootLightMID.Get()) : static_cast<UMaterialInterface*>(TrimMID.Get());
		LeftLeg->ConfigureMaterialSet({ SuitMID.Get(), TrimMID.Get(), BootLight });
		RightLeg->ConfigureMaterialSet({ SuitMID.Get(), TrimMID.Get(), BootLight });
	}
	if (VisorMID) { Visor->ConfigureMaterialSet({ VisorMID.Get() }); }
}

// ---------------------------------------------------------------------------------------------
// EVA, boarding
// ---------------------------------------------------------------------------------------------

void APersonPawn::BeginEVA(AStarshipPawn* FromCraft, const FTransform& Airlock, AStarshipPawn* HullOf)
{
	if (Frame == EPersonFrame::Deck) { SetCameraMode(CameraBeforeDeck); }
	Room = EShipRoom::Count;
	bBeamedDown = false;
	Craft = FromCraft;
	SetActorLocationAndRotation(Airlock.GetLocation(), Airlock.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = FVector::ZeroVector;
	LookPitch = 0.f;
	PlanetScanTimer = 0.f;
	FindWorld();
	if (HullOf)
	{
		HullCraft = HullOf;
		Frame = EPersonFrame::Hull;
		bGrounded = true;
		StickGrace = 0.f;
	}
	else if (GroundPlanet.IsValid())
	{
		Frame = EPersonFrame::Ground;
		bGrounded = true; // the first tick snaps to the ground, or lets go if it is far below
	}
	else if (FromCraft)
	{
		// Out of the hatch into the open: float beside the craft, boots looking for the hull.
		HullCraft = FromCraft;
		Frame = EPersonFrame::Hull;
		bGrounded = false;
		StickGrace = 1.5f;
	}
	else
	{
		Frame = EPersonFrame::Free;
		bGrounded = false;
	}
	if (Frame == EPersonFrame::Hull && HullCraft.IsValid())
	{
		HullLocal = GetActorTransform().GetRelativeTransform(FTransform(HullCraft->GetActorQuat(), HullCraft->GetActorLocation()));
	}
	Limber = bGrounded ? 0.f : 1.f;
}

double APersonPawn::DistanceToAirlock() const
{
	const AStarshipPawn* C = Craft.Get();
	return C ? FVector::Dist(GetActorLocation(), C->GetAirlockTransform().GetLocation()) : -1.0;
}

bool APersonPawn::CanBoard() const
{
	const AStarshipPawn* C = Craft.Get();
	if (!C) { return false; }
	if (Frame == EPersonFrame::Hull && bGrounded && HullCraft.Get() == C) { return true; } // any hatch on the hull
	const double D = DistanceToAirlock();
	return D >= 0.0 && D <= BoardRangeCm;
}

bool APersonPawn::RequestBoard()
{
	AStarshipPawn* C = Craft.Get();
	if (!C)
	{
		Message(TEXT("No craft to board"), FColor::Orange);
		return false;
	}
	if (!CanBoard())
	{
		Message(FString::Printf(TEXT("The %s's airlock is %.0f m away; stand on the hull or come within %.0f m of it"), *C->GetCraftName(), DistanceToAirlock() / 100.0, BoardRangeCm / 100.0), FColor::Orange);
		return false;
	}
	C->OnPersonBoarded(this);
	return true;
}

bool APersonPawn::Use()
{
	AStarshipPawn* C = Craft.Get();
	if (Frame == EPersonFrame::Deck) { return C ? C->PersonUse(this) : false; }
	if (bBeamedDown && C)
	{
		if (FVector::Dist(GetActorLocation(), C->GetActorLocation()) <= C->TransporterRangeCm) { return C->BeamUp(this); }
		Message(FString::Printf(TEXT("The %s is out of transporter range"), *C->GetCraftName()), FColor::Orange);
		return false;
	}
	return RequestBoard();
}

FString APersonPawn::UseHint() const
{
	const AStarshipPawn* C = Craft.Get();
	if (Frame == EPersonFrame::Deck) { return C ? C->PersonUseHint(this) : FString(); }
	if (bBeamedDown && C)
	{
		return FVector::Dist(GetActorLocation(), C->GetActorLocation()) <= C->TransporterRangeCm ? TEXT("F  beam up") : TEXT("out of transporter range");
	}
	return FString();
}

void APersonPawn::PlaceAboard(AStarshipPawn* Ship, EShipRoom InRoom, const FTransform& FeetWorld)
{
	if (!Ship) { return; }
	Craft = Ship;
	HullCraft = Ship;
	Room = InRoom;
	bBeamedDown = false;
	if (Frame != EPersonFrame::Deck)
	{
		// Indoors the lens goes to the helmet: a third-person arm has no room between the walls.
		CameraBeforeDeck = CameraMode;
		SetCameraMode(EPersonCamera::Helmet);
	}
	Frame = EPersonFrame::Deck;
	bGrounded = true;
	Velocity = FVector::ZeroVector;
	StickGrace = 0.f;
	LookPitch = 0.f;
	FreeLookAngles = FVector2D::ZeroVector;
	const FVector Up = FeetWorld.GetRotation().GetUpVector();
	SetActorLocationAndRotation(FeetWorld.GetLocation() + Up * HalfHeightCm, FeetWorld.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	HullLocal = GetActorTransform().GetRelativeTransform(FTransform(Ship->GetActorQuat(), Ship->GetActorLocation()));
	Limber = 0.f;
}

void APersonPawn::TickDeck(float DeltaTime)
{
	AStarshipPawn* C = HullCraft.Get();
	if (!C) { SetFrame(EPersonFrame::Free); bGrounded = false; return; }
	const FVector Up = C->GetActorUpVector();
	const float YawDeg = LookDelta.X + TurnInput * TurnRateDeg * DeltaTime;
	const FQuat Rot = FQuat(Up, FMath::DegreesToRadians(YawDeg)) * GetActorQuat();
	LookPitch = FMath::Clamp(LookPitch + LookDelta.Y, -80.f, 85.f);
	const FVector Fwd = (Rot.GetForwardVector() - Up * FVector::DotProduct(Rot.GetForwardVector(), Up)).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Fwd).GetSafeNormal();

	FVector Want = Fwd * MoveInput.X + Right * MoveInput.Y;
	if (Want.SizeSquared() > 1.0) { Want.Normalize(); }
	Want *= (bSprint || bDebugSprint) ? RunSpeedCm : WalkSpeedCm;
	FVector Vt = Velocity - Up * FVector::DotProduct(Velocity, Up);
	Vt = MoveToward(Vt, Want, WalkAccelCm * DeltaTime);

	// Walls and furniture: probed at the knee and the chest; slide along whatever is in the way, stop if boxed in.
	const FVector Feet = FeetLocation();
	for (int32 Pass = 0; Pass < 3 && Vt.SizeSquared() > 1.0; ++Pass)
	{
		const double Run = Vt.Size() * DeltaTime;
		const FVector Dir = Vt.GetSafeNormal();
		FHitResult Hit;
		bool bHit = false;
		for (float Height : { 45.f, 130.f })
		{
			if (TraceSolid(Feet + Up * Height, Feet + Up * Height + Dir * (Run + 30.0), Hit)) { bHit = true; break; }
		}
		if (!bHit) { break; }
		FVector N = Hit.ImpactNormal - Up * FVector::DotProduct(Hit.ImpactNormal, Up);
		N = N.GetSafeNormal();
		const double Into = FVector::DotProduct(Vt, N);
		if (Pass == 2 || N.IsNearlyZero() || Into >= 0.0) { Vt = FVector::ZeroVector; break; }
		Vt -= N * Into;
	}
	Velocity = Vt;
	FVector NextFeet = Feet + Vt * DeltaTime;
	// The floor under the next step: a kerb or a pad up to StepUpCm is taken, a drop found below; nothing found
	// keeps the height (a doorway's sill, a gap in the set).
	FHitResult Floor;
	if (TraceSolid(NextFeet + Up * StepUpCm, NextFeet - Up * 120.0, Floor)) { NextFeet = Floor.ImpactPoint; }
	SetActorLocationAndRotation(NextFeet + Up * HalfHeightCm, StandingQuat(Up, Fwd), false, nullptr, ETeleportType::TeleportPhysics);
	bGrounded = true;
}

void APersonPawn::Away(const FString& Command)
{
	DebugCommand = Command;
}

void APersonPawn::Message(const FString& Text, FColor Color, float Seconds) const
{
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, Seconds, Color, Text); }
}

FString APersonPawn::FrameDescription() const
{
	const AStarshipPawn* H = HullCraft.Get();
	const APlanetBody* P = GroundPlanet.Get();
	switch (Frame)
	{
	case EPersonFrame::Hull:
		return bGrounded ? FString::Printf(TEXT("ON THE HULL  %s   boots holding"), H ? *H->GetCraftName().ToUpper() : TEXT("")) : FString::Printf(TEXT("FLOATING BESIDE  %s"), H ? *H->GetCraftName().ToUpper() : TEXT(""));
	case EPersonFrame::Deck:
		return FString::Printf(TEXT("ABOARD  %s   %s"), H ? *H->GetCraftName().ToUpper() : TEXT(""), *UShipInteriorComponent::RoomName(Room).ToUpper());
	case EPersonFrame::Ground:
		if (bGrounded) { return FString::Printf(TEXT("ON THE GROUND  %s"), P ? *P->GetDisplayName().ToUpper() : TEXT("")); }
		return FString::Printf(TEXT("%s  %s"), P && FVector::DotProduct(Velocity, (GetActorLocation() - P->GetActorLocation()).GetSafeNormal()) > 0.0 ? TEXT("RISING") : TEXT("FALLING"), P ? *P->GetDisplayName().ToUpper() : TEXT(""));
	default:
		return TEXT("FREE FLIGHT");
	}
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void APersonPawn::AddLookInput(float YawDegrees, float PitchDegrees)
{
	if (bFreeLook)
	{
		FreeLookAngles.X = FMath::Clamp(FreeLookAngles.X + YawDegrees, -170.f, 170.f);
		FreeLookAngles.Y = FMath::Clamp(FreeLookAngles.Y + PitchDegrees, -80.f, 80.f);
	}
	else
	{
		LookDelta += FVector2D(YawDegrees, PitchDegrees);
	}
}

void APersonPawn::Jump() { bJumpQueued = true; }

void APersonPawn::ToggleBoots()
{
	bBootsArmed = !bBootsArmed;
	if (!bBootsArmed && Frame == EPersonFrame::Hull && bGrounded)
	{
		// Let go of the hull where we stand.
		bGrounded = false;
		StickGrace = 1.f;
	}
	Message(bBootsArmed ? TEXT("Magnetic boots armed: they hold on any hull they touch") : TEXT("Magnetic boots off"), FColor::Cyan, 4.f);
}

void APersonPawn::SetCameraMode(EPersonCamera Mode)
{
	CameraMode = Mode;
	const bool bThird = Mode == EPersonCamera::Third;
	if (ThirdCamera) { ThirdCamera->SetActive(bThird); }
	if (HelmetCamera) { HelmetCamera->SetActive(!bThird); }
	// From inside the helmet the figure is in the way of the view.
	bFigureShown = bThird;
	for (UDynamicMeshComponent* P : { Torso.Get(), Visor.Get(), LeftArm.Get(), RightArm.Get(), LeftLeg.Get(), RightLeg.Get() })
	{
		if (P) { P->SetVisibility(bThird); }
	}
}

void APersonPawn::CycleCamera() { SetCameraMode(CameraMode == EPersonCamera::Third ? EPersonCamera::Helmet : EPersonCamera::Third); }

void APersonPawn::ResetCamera()
{
	FreeLookAngles = FVector2D::ZeroVector;
	Zoom = 1.f;
	SetCameraMode(EPersonCamera::Third);
}

void APersonPawn::AdjustZoom(float Steps) { Zoom = FMath::Clamp(Zoom * FMath::Pow(0.85f, Steps), 0.35f, 4.f); }

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void APersonPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	DeltaTime = FMath::Min(DeltaTime, 0.1f);
	ProcessDebugCommand();
	if (DebugMoveTime > 0.f) { DebugMoveTime -= DeltaTime; MoveInput = DebugMove; }
	if (DebugVerticalTime > 0.f) { DebugVerticalTime -= DeltaTime; VerticalInput = DebugVertical; }
	if (DebugLookTime > 0.f) { DebugLookTime -= DeltaTime; LookDelta += DebugLook * DeltaTime; }
	StickGrace = FMath::Max(StickGrace - DeltaTime, 0.f);

	FindWorld();
	CarryWithWorld(DeltaTime);

	LastJetDemand = FVector::ZeroVector;
	LastTorqueDemand = FVector::ZeroVector;
	switch (Frame)
	{
	case EPersonFrame::Hull: TickHull(DeltaTime); break;
	case EPersonFrame::Deck: TickDeck(DeltaTime); break;
	case EPersonFrame::Ground: if (bGrounded) { TickWalking(DeltaTime); } else { TickFloating(DeltaTime); } break;
	default: TickFloating(DeltaTime); break;
	}
	if ((Frame == EPersonFrame::Hull || Frame == EPersonFrame::Deck) && HullCraft.IsValid())
	{
		HullLocal = GetActorTransform().GetRelativeTransform(FTransform(HullCraft->GetActorQuat(), HullCraft->GetActorLocation()));
	}
	if (GroundPlanet.IsValid())
	{
		AltitudeCm = FVector::Dist(FeetLocation(), GroundPlanet->GetActorLocation()) - SurfaceRadiusAt(FeetLocation());
	}

	UpdateFigure(DeltaTime);
	UpdateCamera(DeltaTime);

	MoveInput = FVector2D::ZeroVector;
	VerticalInput = 0.f;
	TurnInput = 0.f;
	LookDelta = FVector2D::ZeroVector;
	if (bGrounded) { bBraking = false; }
	bJumpQueued = false;
}

FVector APersonPawn::FeetLocation() const { return GetActorLocation() - GetActorUpVector() * HalfHeightCm; }

double APersonPawn::SurfaceRadiusAt(const FVector& WorldPos) const
{
	const APlanetBody* P = GroundPlanet.Get();
	return P ? P->GetSurfaceRadiusCm(WorldPos) : 0.0;
}

FVector APersonPawn::CraftVelocity(const AStarshipPawn* C) const
{
	return C && C->Movement ? C->Movement->Velocity : FVector::ZeroVector;
}

void APersonPawn::FindWorld()
{
	PlanetScanTimer -= GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	if (PlanetScanTimer <= 0.f)
	{
		PlanetScanTimer = 1.f;
		Planets.Reset();
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APlanetBody> It(World); It; ++It) { Planets.Add(*It); }
		}
	}
	const FVector Here = GetActorLocation();
	APlanetBody* Near = nullptr;
	double NearDist = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<APlanetBody>& W : Planets)
	{
		APlanetBody* P = W.Get();
		if (!P) { continue; }
		const double D = FVector::Dist(Here, P->GetActorLocation());
		if (D < P->GetRadiusCm() * 1.5 && D < NearDist) { Near = P; NearDist = D; }
	}
	GroundPlanet = Near;
	if (Near)
	{
		const double R = Near->GetRadiusCm();
		GravityCmS2 = Near->GetSurfaceGravityCmS2() * FMath::Square(R / FMath::Max(NearDist, R));
	}
	else
	{
		GravityCmS2 = 0.0;
		AltitudeCm = -1.0;
	}
	// The world's reach decides the frame when no hull holds us.
	if (Frame == EPersonFrame::Ground && !Near) { SetFrame(EPersonFrame::Free); bGrounded = false; }
	else if (Frame == EPersonFrame::Free && Near) { SetFrame(EPersonFrame::Ground); bGrounded = false; }
}

void APersonPawn::SetFrame(EPersonFrame NewFrame)
{
	if (NewFrame == Frame) { return; }
	// Velocities are relative to the frame: hand them over when a hull is left or joined.
	const bool bWasCraft = Frame == EPersonFrame::Hull || Frame == EPersonFrame::Deck;
	const bool bToCraft = NewFrame == EPersonFrame::Hull || NewFrame == EPersonFrame::Deck;
	if (bWasCraft)
	{
		Velocity += CraftVelocity(HullCraft.Get());
		if (!bToCraft) { HullCraft = nullptr; }
	}
	if (bToCraft && !bWasCraft)
	{
		Velocity -= CraftVelocity(HullCraft.Get());
	}
	Frame = NewFrame;
}

void APersonPawn::CarryWithWorld(float DeltaTime)
{
	if (Frame == EPersonFrame::Hull || Frame == EPersonFrame::Deck)
	{
		AStarshipPawn* C = HullCraft.Get();
		if (!C)
		{
			SetFrame(GroundPlanet.IsValid() ? EPersonFrame::Ground : EPersonFrame::Free);
			bGrounded = false;
			return;
		}
		const FTransform W = HullLocal * FTransform(C->GetActorQuat(), C->GetActorLocation());
		SetActorLocationAndRotation(W.GetLocation(), W.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		// Keep the world's turn in step for when we let go of the hull.
		CarryPlanet = GroundPlanet;
		if (APlanetBody* G = GroundPlanet.Get()) { CarryPlanetQuat = G->GetActorQuat(); }
		return;
	}
	APlanetBody* P = GroundPlanet.Get();
	if (!P) { CarryPlanet = nullptr; return; }
	// Ride the world's spin as the craft do: full below 1.05 radii, none above 1.35. The turn is the planet's own
	// since our last tick, not its rate times our (clamped) frame time: a long frame while the terrain builds
	// would otherwise leave us behind the ground, and the shuttle we stepped out of, by metres.
	const FQuat PlanetQ = P->GetActorQuat();
	if (CarryPlanet.Get() != P)
	{
		CarryPlanet = P;
		CarryPlanetQuat = PlanetQ;
		return;
	}
	FQuat Q = PlanetQ * CarryPlanetQuat.Inverse();
	CarryPlanetQuat = PlanetQ;
	const FVector Centre = P->GetActorLocation();
	const FVector Rv = GetActorLocation() - Centre;
	const double Radii = Rv.Size() / FMath::Max(P->GetRadiusCm(), 1.0);
	const double Carry = FMath::Clamp((1.35 - Radii) / 0.30, 0.0, 1.0);
	if (Carry <= 0.0 || Q.IsIdentity(1e-12)) { return; }
	if (Carry < 1.0) { Q = FQuat::Slerp(FQuat::Identity, Q, (float)Carry); }
	SetActorLocationAndRotation(Centre + Q.RotateVector(Rv), Q * GetActorQuat(), false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = Q.RotateVector(Velocity);
}

bool APersonPawn::TraceSolid(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PersonSolid), true, this);
	return World->LineTraceSingleByChannel(Hit, Start, End, BootsChannel, Params);
}

bool APersonPawn::TraceBoots(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PersonBoots), true, this);
	return World->LineTraceSingleByChannel(Hit, Start, End, BootsChannel, Params) && Cast<AStarshipPawn>(Hit.GetActor()) != nullptr;
}

void APersonPawn::StandOn(const FVector& Feet, const FVector& Normal, bool bSnapUp)
{
	const FVector Up0 = GetActorUpVector();
	FVector Up1 = Normal.GetSafeNormal();
	if (Up1.IsNearlyZero()) { Up1 = Up0; }
	if (!bSnapUp)
	{
		// Walking round a curve: the body follows the surface over a few frames, not in one.
		const float Dt = GetWorld() ? FMath::Min(GetWorld()->GetDeltaSeconds(), 0.1f) : 0.016f;
		Up1 = FMath::Lerp(Up0, Up1, 1.f - FMath::Exp(-12.f * Dt)).GetSafeNormal();
	}
	const FQuat Rot = StandingQuat(Up1, GetActorForwardVector());
	SetActorLocationAndRotation(Feet + Up1 * HalfHeightCm, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	// Motion stays in the surface.
	Velocity -= Up1 * FVector::DotProduct(Velocity, Up1);
	bGrounded = true;
}

bool APersonPawn::TryStick(bool bAhead)
{
	if (!bBootsArmed || StickGrace > 0.f) { return false; }
	const FVector Feet = FeetLocation();
	const FVector Up = GetActorUpVector();
	FHitResult Hit;
	bool bHit = TraceBoots(Feet + Up * 20.f, Feet - Up * BootReachCm, Hit);
	if (!bHit && bAhead && Velocity.SizeSquared() > 1.0)
	{
		const FVector Dir = Velocity.GetSafeNormal();
		bHit = TraceBoots(GetActorLocation(), GetActorLocation() + Dir * (HalfHeightCm + 60.f), Hit);
	}
	if (!bHit) { return false; }
	AStarshipPawn* C = Cast<AStarshipPawn>(Hit.GetActor());
	if (!C) { return false; }
	if (Frame != EPersonFrame::Hull || HullCraft.Get() != C)
	{
		if (Frame == EPersonFrame::Hull) { Velocity += CraftVelocity(HullCraft.Get()); }
		HullCraft = C;
		Frame = EPersonFrame::Hull;
		Velocity -= CraftVelocity(C);
	}
	StandOn(Hit.ImpactPoint, Hit.ImpactNormal, true);
	Velocity *= 0.3; // the boots take the arrival
	Message(FString::Printf(TEXT("Boots on the %s"), *C->GetCraftName()), FColor::Cyan, 3.f);
	return true;
}

void APersonPawn::TickWalking(float DeltaTime)
{
	APlanetBody* P = GroundPlanet.Get();
	if (!P) { bGrounded = false; return; }
	const FVector Centre = P->GetActorLocation();
	FVector Here = GetActorLocation();
	FVector Up = (Here - Centre).GetSafeNormal();

	// Turn: mouse and the turn keys yaw the body about the local vertical; the mouse's pitch raises the eyes.
	const float YawDeg = LookDelta.X + TurnInput * TurnRateDeg * DeltaTime;
	FQuat Rot = FQuat(Up, FMath::DegreesToRadians(YawDeg)) * GetActorQuat();
	LookPitch = FMath::Clamp(LookPitch + LookDelta.Y, -80.f, 85.f);
	const FVector Fwd = (Rot.GetForwardVector() - Up * FVector::DotProduct(Rot.GetForwardVector(), Up)).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Fwd).GetSafeNormal();

	FVector Want = Fwd * MoveInput.X + Right * MoveInput.Y;
	if (Want.SizeSquared() > 1.0) { Want.Normalize(); }
	Want *= (bSprint || bDebugSprint) ? RunSpeedCm : WalkSpeedCm;
	FVector Vt = Velocity - Up * FVector::DotProduct(Velocity, Up);
	Vt = MoveToward(Vt, Want, WalkAccelCm * DeltaTime);
	Velocity = Vt;

	if (bJumpQueued || VerticalInput > 0.1f)
	{
		// A jump, or the pack asked to lift: either way the ground is left (the pack then carries on in the air).
		Velocity += Up * (bJumpQueued ? JumpSpeedCm : 30.f);
		bGrounded = false;
		JumpStartRadius = (Here - Centre).Size();
		LastJumpHeightCm = 0.0;
		SetActorRotation(Rot, ETeleportType::TeleportPhysics);
		return;
	}

	FVector Next = Here + Velocity * DeltaTime;
	const double Run = Vt.Size() * DeltaTime;
	if (Run > 1e-3)
	{
		// Too steep to climb: stop at the foot of it. Downhill always goes.
		const double Rise = SurfaceRadiusAt(Next) - SurfaceRadiusAt(Here);
		if (Rise > Run * FMath::Tan(FMath::DegreesToRadians(MaxSlopeDeg)) + 0.5)
		{
			Velocity = FVector::ZeroVector;
			Next = Here;
		}
		else
		{
			// A hull or a wall in the way (the shuttle's flank, a building): the legs stop; the boots only take a
			// hull from the air. A kerb under half a metre is stepped up by the floor probe below.
			FHitResult Hit;
			const FVector Feet = Here - Up * HalfHeightCm;
			if (TraceSolid(Feet + Up * 55.f, Feet + Up * 55.f + Vt.GetSafeNormal() * (Run + 25.0), Hit))
			{
				Velocity = FVector::ZeroVector;
				Next = Here;
			}
		}
	}
	// Onto the ground, or off a ledge.
	const FVector Rv = Next - Centre;
	const double Dist = Rv.Size();
	const FVector UpN = Rv / FMath::Max(Dist, 1.0);
	double Target = SurfaceRadiusAt(Next) + HalfHeightCm;
	{
		// A roof, a pad or a kerb under the feet stands in for the ground where it is higher.
		FHitResult Floor;
		const FVector Head = Next + UpN * 50.0, Below = Next - UpN * (HalfHeightCm + 120.0);
		if (TraceSolid(Head, Below, Floor))
		{
			const double FloorR = (Floor.ImpactPoint - Centre).Size();
			if (FloorR > Target - HalfHeightCm) { Target = FloorR + HalfHeightCm; }
		}
	}
	if (Target < Dist - 60.0)
	{
		bGrounded = false; // the ground fell away: fall
	}
	else
	{
		Next = Centre + UpN * Target;
	}
	Rot = StandingQuat(UpN, Fwd);
	SetActorLocationAndRotation(Next, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	// Standing on the ground beside a hull: a hull under the next step (a skid, a ramp) is taken by the boots;
	// walking into a flank is a wall, not a climb.
	if (bGrounded) { TryStick(false); }
}

FVector APersonPawn::IntegrateFloating(float DeltaTime, const FVector& Up, bool bFeetDown)
{
	const FQuat Q = GetActorQuat();
	FVector Fwd = Q.GetForwardVector(), Right = Q.GetRightVector(), UpB = Q.GetUpVector();
	if (bFeetDown)
	{
		Fwd = (Fwd - Up * FVector::DotProduct(Fwd, Up)).GetSafeNormal();
		Right = FVector::CrossProduct(Up, Fwd).GetSafeNormal();
		UpB = Up;
	}
	FVector Demand(MoveInput.X, MoveInput.Y, VerticalInput);
	if (Demand.SizeSquared() > 1.0) { Demand.Normalize(); }
	if (!Demand.IsNearlyZero(0.01)) { bBraking = false; }
	Velocity += (Fwd * Demand.X + Right * Demand.Y + UpB * Demand.Z) * JetAccelCm * DeltaTime;
	if (GroundPlanet.IsValid()) { Velocity -= Up * GravityCmS2 * DeltaTime; }
	if (bBraking)
	{
		// The pack fires against the motion: exponential, with a floor so the last centimetres go too.
		const FVector Before = Velocity;
		Velocity *= FMath::Exp(-BrakeRate * DeltaTime);
		Velocity = MoveToward(Velocity, FVector::ZeroVector, JetAccelCm * DeltaTime);
		const FVector ThrustDir = -(Before - Velocity).GetSafeNormal(); // for the plumes: the jets fire against the motion
		Demand = FVector(FVector::DotProduct(ThrustDir, Fwd), FVector::DotProduct(ThrustDir, Right), FVector::DotProduct(ThrustDir, UpB));
		if (Velocity.SizeSquared() < 4.0) { Velocity = FVector::ZeroVector; bBraking = false; }
	}
	if (!GroundPlanet.IsValid()) { Velocity = Velocity.GetClampedToMaxSize(JetMaxSpeedCm); }
	LastJetDemand = Demand;
	return GetActorLocation() + Velocity * DeltaTime;
}

void APersonPawn::TickFloating(float DeltaTime)
{
	APlanetBody* P = GroundPlanet.Get();
	const FVector Centre = P ? P->GetActorLocation() : FVector::ZeroVector;
	const bool bFeetDown = P && GravityCmS2 > 100.0;
	FVector Up = bFeetDown ? (GetActorLocation() - Centre).GetSafeNormal() : GetActorUpVector();

	FQuat Rot = GetActorQuat();
	if (bFeetDown)
	{
		// Falling feet first: yaw about the vertical, eyes up and down; the body eases upright.
		const float YawDeg = LookDelta.X + TurnInput * TurnRateDeg * DeltaTime;
		Rot = FQuat(Up, FMath::DegreesToRadians(YawDeg)) * Rot;
		LookPitch = FMath::Clamp(LookPitch + LookDelta.Y, -80.f, 85.f);
		const FQuat Want = StandingQuat(Up, Rot.GetForwardVector());
		Rot = FQuat::Slerp(Rot, Want, 1.f - FMath::Exp(-5.f * DeltaTime)).GetNormalized();
		TurnRates = FRotator::ZeroRotator;
	}
	else
	{
		// Free flight: the mouse pitches and yaws the whole person, the turn keys roll, as the ships steer.
		const float A = 1.f - FMath::Exp(-TurnResponse * DeltaTime);
		TurnRates.Roll = FMath::Lerp(TurnRates.Roll, TurnInput * TurnRateDeg, A);
		TurnRates.Pitch = FMath::Lerp(TurnRates.Pitch, (DebugSteerTime > 0.f ? DebugSteer.X : 0.f) * TurnRateDeg, A);
		TurnRates.Yaw = FMath::Lerp(TurnRates.Yaw, (DebugSteerTime > 0.f ? DebugSteer.Y : 0.f) * TurnRateDeg, A);
		if (DebugSteerTime > 0.f) { DebugSteerTime -= DeltaTime; TurnRates.Roll = FMath::Lerp(TurnRates.Roll, DebugSteer.Z * TurnRateDeg, A); }
		const FQuat Delta(FRotator(LookDelta.Y + TurnRates.Pitch * DeltaTime, LookDelta.X + TurnRates.Yaw * DeltaTime, TurnRates.Roll * DeltaTime));
		Rot = (Rot * Delta).GetNormalized();
		LookPitch = FMath::Lerp(LookPitch, 0.f, 1.f - FMath::Exp(-6.f * DeltaTime));
		LastTorqueDemand = FVector(LookDelta.Y * 0.2f, LookDelta.X * 0.2f, TurnInput);
		Up = Rot.GetUpVector();
	}
	SetActorRotation(Rot, ETeleportType::TeleportPhysics);

	FVector Next = IntegrateFloating(DeltaTime, Up, bFeetDown);
	if (P)
	{
		const FVector Rv = Next - Centre;
		const double Dist = Rv.Size();
		const FVector UpN = Rv / FMath::Max(Dist, 1.0);
		if (JumpStartRadius > 0.0) { LastJumpHeightCm = FMath::Max(LastJumpHeightCm, Dist - JumpStartRadius); }
		const double Target = SurfaceRadiusAt(Next) + HalfHeightCm;
		if (Dist <= Target)
		{
			// Touchdown: the ground takes the vertical motion.
			Next = Centre + UpN * Target;
			if (Frame != EPersonFrame::Ground) { SetFrame(EPersonFrame::Ground); }
			bGrounded = true;
			JumpStartRadius = 0.0;
			Velocity -= UpN * FVector::DotProduct(Velocity, UpN);
			Rot = StandingQuat(UpN, Rot.GetForwardVector());
		}
	}
	SetActorLocationAndRotation(Next, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	if (!bGrounded) { TryStick(); }
}

void APersonPawn::TickHull(float DeltaTime)
{
	AStarshipPawn* C = HullCraft.Get();
	if (!C) { return; }
	if (bGrounded)
	{
		FVector Up = GetActorUpVector();
		const float YawDeg = LookDelta.X + TurnInput * TurnRateDeg * DeltaTime;
		FQuat Rot = FQuat(Up, FMath::DegreesToRadians(YawDeg)) * GetActorQuat();
		LookPitch = FMath::Clamp(LookPitch + LookDelta.Y, -80.f, 85.f);
		SetActorRotation(Rot, ETeleportType::TeleportPhysics);
		const FVector Fwd = (Rot.GetForwardVector() - Up * FVector::DotProduct(Rot.GetForwardVector(), Up)).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(Up, Fwd).GetSafeNormal();

		FVector Want = Fwd * MoveInput.X + Right * MoveInput.Y;
		if (Want.SizeSquared() > 1.0) { Want.Normalize(); }
		Want *= (bSprint || bDebugSprint) ? RunSpeedCm : WalkSpeedCm;
		FVector Vt = Velocity - Up * FVector::DotProduct(Velocity, Up);
		Vt = MoveToward(Vt, Want, WalkAccelCm * DeltaTime);
		Velocity = Vt;

		if (bJumpQueued || VerticalInput > 0.1f)
		{
			// Push off (or the pack asked to lift): the boots let go and the pack has the rest.
			Velocity += Up * PushOffSpeedCm;
			bGrounded = false;
			StickGrace = 1.f;
			return;
		}
		const FVector Feet = FeetLocation();
		const double Run = Vt.Size() * DeltaTime;
		const FVector NextFeet = Feet + Vt * DeltaTime;
		FHitResult Hit;
		if (Run > 1e-3 && TraceBoots(Feet + Up * 40.f, Feet + Up * 40.f + Vt.GetSafeNormal() * (Run + 20.0), Hit))
		{
			// A wall ahead: the boots take it and we walk up it. The body snaps to the new normal at once: a blended
			// up would send the next tick's trace along the wall instead of into it.
			StandOn(Hit.ImpactPoint + Hit.ImpactNormal, Hit.ImpactNormal, true);
			Velocity = FVector::ZeroVector;
		}
		else if (TraceBoots(NextFeet + Up * StepUpCm, NextFeet - Up * BootReachCm, Hit))
		{
			StandOn(Hit.ImpactPoint, Hit.ImpactNormal, false);
		}
		else if (Run > 1e-3 && TraceBoots(NextFeet - Up * 60.f, NextFeet - Up * 60.f - Vt.GetSafeNormal() * (Run + 90.0), Hit))
		{
			// The edge of a deck or the saucer rim: the face under it is hull too, so walk round onto it.
			StandOn(Hit.ImpactPoint + Hit.ImpactNormal, Hit.ImpactNormal, true);
			Velocity = FVector::ZeroVector;
		}
		else if (TraceBoots(NextFeet + Up * StepUpCm, NextFeet - Up * (BootReachCm * 5.f), Hit))
		{
			// A ledge: hull further below. Let go and drop toward it; the boots take it on arrival.
			SetActorLocation(NextFeet + Up * HalfHeightCm, false, nullptr, ETeleportType::TeleportPhysics);
			bGrounded = false;
			Velocity = Vt * 0.25 - Up * 200.0;
		}
		else
		{
			// Walked off into the open: carry on in a straight line; the boots keep reaching for the hull.
			SetActorLocation(NextFeet + Up * HalfHeightCm, false, nullptr, ETeleportType::TeleportPhysics);
			bGrounded = false;
			UE_LOG(LogTemp, Log, TEXT("Person: boots lost the hull at %s (feet), up %s"), *NextFeet.ToString(), *Up.ToString());
		}
		return;
	}

	// Floating beside the craft, in its frame.
	APlanetBody* P = GroundPlanet.Get();
	const bool bFeetDown = P && GravityCmS2 > 100.0;
	FVector Up = bFeetDown ? (GetActorLocation() - P->GetActorLocation()).GetSafeNormal() : GetActorUpVector();
	FQuat Rot = GetActorQuat();
	if (bFeetDown)
	{
		const float YawDeg = LookDelta.X + TurnInput * TurnRateDeg * DeltaTime;
		Rot = FQuat(Up, FMath::DegreesToRadians(YawDeg)) * Rot;
		LookPitch = FMath::Clamp(LookPitch + LookDelta.Y, -80.f, 85.f);
		Rot = FQuat::Slerp(Rot, StandingQuat(Up, Rot.GetForwardVector()), 1.f - FMath::Exp(-5.f * DeltaTime)).GetNormalized();
	}
	else
	{
		const float A = 1.f - FMath::Exp(-TurnResponse * DeltaTime);
		TurnRates.Roll = FMath::Lerp(TurnRates.Roll, TurnInput * TurnRateDeg, A);
		TurnRates.Pitch = FMath::Lerp(TurnRates.Pitch, (DebugSteerTime > 0.f ? DebugSteer.X : 0.f) * TurnRateDeg, A);
		TurnRates.Yaw = FMath::Lerp(TurnRates.Yaw, (DebugSteerTime > 0.f ? DebugSteer.Y : 0.f) * TurnRateDeg, A);
		if (DebugSteerTime > 0.f) { DebugSteerTime -= DeltaTime; TurnRates.Roll = FMath::Lerp(TurnRates.Roll, DebugSteer.Z * TurnRateDeg, A); }
		const FQuat Delta(FRotator(LookDelta.Y + TurnRates.Pitch * DeltaTime, LookDelta.X + TurnRates.Yaw * DeltaTime, TurnRates.Roll * DeltaTime));
		Rot = (Rot * Delta).GetNormalized();
		LookPitch = FMath::Lerp(LookPitch, 0.f, 1.f - FMath::Exp(-6.f * DeltaTime));
		LastTorqueDemand = FVector(LookDelta.Y * 0.2f, LookDelta.X * 0.2f, TurnInput);
		Up = Rot.GetUpVector();
	}
	SetActorRotation(Rot, ETeleportType::TeleportPhysics);
	FVector Next = IntegrateFloating(DeltaTime, Up, bFeetDown);
	if (P)
	{
		const FVector Centre = P->GetActorLocation();
		const FVector Rv = Next - Centre;
		const double Dist = Rv.Size();
		const FVector UpN = Rv / FMath::Max(Dist, 1.0);
		const double Target = SurfaceRadiusAt(Next) + HalfHeightCm;
		if (Dist <= Target)
		{
			Next = Centre + UpN * Target;
			SetFrame(EPersonFrame::Ground);
			bGrounded = true;
			Velocity -= UpN * FVector::DotProduct(Velocity, UpN);
			Rot = StandingQuat(UpN, Rot.GetForwardVector());
			SetActorLocationAndRotation(Next, Rot, false, nullptr, ETeleportType::TeleportPhysics);
			return;
		}
	}
	SetActorLocationAndRotation(Next, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	if (TryStick()) { return; }
	// Drifted clear of the craft: the open, or the world below, has us now.
	const double Clear = C->GetShipLengthCm() + 5000.0;
	if (FVector::Dist(Next, C->GetActorLocation()) > Clear)
	{
		SetFrame(P ? EPersonFrame::Ground : EPersonFrame::Free);
		bGrounded = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Figure and camera
// ---------------------------------------------------------------------------------------------

void APersonPawn::UpdateFigure(float DeltaTime)
{
	const FVector Up = GetActorUpVector();
	const FVector Vt = Velocity - Up * FVector::DotProduct(Velocity, Up);
	const float Speed = bGrounded ? (float)Vt.Size() : 0.f;
	Stride = FMath::Clamp(Speed / WalkSpeedCm, 0.f, 1.6f);
	WalkPhase += Speed / 140.f * 2.f * PI * DeltaTime;
	if (Speed < 5.f) { WalkPhase = FMath::Lerp(WalkPhase, FMath::RoundToFloat(WalkPhase / PI) * PI, 1.f - FMath::Exp(-8.f * DeltaTime)); }
	Limber = FMath::Lerp(Limber, bGrounded ? 0.f : 1.f, 1.f - FMath::Exp(-4.f * DeltaTime));

	const float Swing = FMath::Sin(WalkPhase);
	const float LegAmp = 30.f * FMath::Min(Stride, 1.2f);
	const float ArmAmp = 22.f * FMath::Min(Stride, 1.2f);
	// Walking: legs and arms swing in opposition. Floating: limbs loose and a little apart, arms out for the pack.
	const FRotator LegL = FMath::Lerp(FRotator(Swing * LegAmp, 0.f, 0.f), FRotator(-14.f, 0.f, -8.f), Limber);
	const FRotator LegR = FMath::Lerp(FRotator(-Swing * LegAmp, 0.f, 0.f), FRotator(-6.f, 0.f, 8.f), Limber);
	const FRotator ArmL = FMath::Lerp(FRotator(-Swing * ArmAmp, 0.f, 0.f), FRotator(-20.f, 0.f, -28.f), Limber);
	const FRotator ArmR = FMath::Lerp(FRotator(Swing * ArmAmp, 0.f, 0.f), FRotator(-20.f, 0.f, 28.f), Limber);
	if (LeftLeg) { LeftLeg->SetRelativeRotation(LegL); }
	if (RightLeg) { RightLeg->SetRelativeRotation(LegR); }
	if (LeftArm) { LeftArm->SetRelativeRotation(ArmL); }
	if (RightArm) { RightArm->SetRelativeRotation(ArmR); }

	if (BootLightMID)
	{
		const bool bHolding = Frame == EPersonFrame::Hull && bGrounded;
		BootLightMID->SetScalarParameterValue(TEXT("Intensity"), bHolding ? 6.f : (bBootsArmed ? 1.2f : 0.f));
	}
	if (Jets)
	{
		Jets->SetDemand(bGrounded ? FVector::ZeroVector : LastTorqueDemand, bGrounded ? FVector::ZeroVector : LastJetDemand);
	}
}

void APersonPawn::UpdateCamera(float DeltaTime)
{
	if (!bFreeLook && DebugLookTime <= 0.f)
	{
		FreeLookAngles = FMath::Lerp(FreeLookAngles, FVector2D::ZeroVector, 1.f - FMath::Exp(-6.f * DeltaTime));
	}
	const FRotator Look(LookPitch + FreeLookAngles.Y, FreeLookAngles.X, 0.f);
	if (CameraMode == EPersonCamera::Third && CameraArm && ThirdCamera)
	{
		CameraArm->SetRelativeRotation(Look);
		CameraArm->TargetArmLength = ThirdPersonDistanceCm * Zoom;
		CameraArm->SocketOffset = FVector(0.f, 0.f, 30.f * Zoom);
		// The terrain carries no collision: lift the pivot when the lens would dip under the ground.
		FVector Lift = CameraArm->TargetOffset;
		if (const APlanetBody* Ground = GroundPlanet.Get())
		{
			const FVector Cam = ThirdCamera->GetComponentLocation();
			const FVector Rv = Cam - Ground->GetActorLocation();
			const double D = Rv.Size();
			if (D > 1.0)
			{
				const FVector Radial = Rv / D;
				const double Clearance = D - (Ground->GetSurfaceRadiusCm(Cam) + 30.0);
				if (Clearance < 0.0) { Lift += Radial * (-Clearance); }
				else if (!Lift.IsNearlyZero())
				{
					const double Drop = FMath::Min(Clearance, Lift.Size() * 3.0 * DeltaTime);
					Lift -= Lift.GetSafeNormal() * Drop;
				}
			}
		}
		else
		{
			Lift = FVector::ZeroVector;
		}
		CameraArm->TargetOffset = Lift;
		// Squeezed up against a hull, the lens would be inside the backpack: show the figure only with room to see it.
		const float Actual = (float)FVector::Dist(ThirdCamera->GetComponentLocation(), CameraArm->GetComponentLocation());
		const bool bShowFigure = Actual > 120.f;
		if (bShowFigure != bFigureShown)
		{
			bFigureShown = bShowFigure;
			for (UDynamicMeshComponent* P : { Torso.Get(), Visor.Get(), LeftArm.Get(), RightArm.Get(), LeftLeg.Get(), RightLeg.Get() })
			{
				if (P) { P->SetVisibility(bShowFigure); }
			}
		}
	}
	if (HelmetCamera)
	{
		HelmetCamera->SetRelativeRotation(Look);
	}
}

// ---------------------------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------------------------

void APersonPawn::ProcessDebugCommand()
{
	if (DebugCommand.IsEmpty()) { return; }
	TArray<FString> Args;
	DebugCommand.TrimStartAndEnd().ParseIntoArrayWS(Args);
	const FString Cmd = DebugCommand;
	DebugCommand.Empty();
	if (Args.Num() == 0) { return; }
	const FString Op = Args[0].ToLower();
	auto Arg = [&Args](int32 Index, float Default) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default; };
	auto ArgStr = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? Args[Index].ToLower() : FString(); };
	UE_LOG(LogTemp, Log, TEXT("Person debug command: %s"), *Cmd);

	if (Op == TEXT("move")) { DebugMove = FVector2D(Arg(1, 0.f), Arg(2, 0.f)); DebugMoveTime = Arg(3, 2.f); }
	else if (Op == TEXT("vert")) { DebugVertical = Arg(1, 0.f); DebugVerticalTime = Arg(2, 2.f); }
	else if (Op == TEXT("steer")) { DebugSteer = FVector(Arg(1, 0.f), Arg(2, 0.f), Arg(3, 0.f)); DebugSteerTime = Arg(4, 2.f); }
	else if (Op == TEXT("look"))
	{
		// look <yaw> <pitch> [seconds]: degrees, spread over the seconds (default at once).
		const float S = Arg(3, 0.f);
		if (S > 0.f) { DebugLook = FVector2D(Arg(1, 0.f), Arg(2, 0.f)) / S; DebugLookTime = S; }
		else { LookDelta += FVector2D(Arg(1, 0.f), Arg(2, 0.f)); }
	}
	else if (Op == TEXT("jump")) { Jump(); }
	else if (Op == TEXT("boots")) { if ((ArgStr(1) == TEXT("on")) != bBootsArmed) { ToggleBoots(); } }
	else if (Op == TEXT("brake")) { Brake(); }
	else if (Op == TEXT("sprint")) { bDebugSprint = ArgStr(1) != TEXT("off"); }
	else if (Op == TEXT("camera")) { SetCameraMode(ArgStr(1) == TEXT("helmet") || ArgStr(1) == TEXT("first") ? EPersonCamera::Helmet : EPersonCamera::Third); }
	else if (Op == TEXT("freelook")) { bFreeLook = ArgStr(1) != TEXT("off"); }
	else if (Op == TEXT("zoom")) { Zoom = FMath::Clamp(Arg(1, 1.f), 0.35f, 4.f); }
	else if (Op == TEXT("board")) { RequestBoard(); }
	else if (Op == TEXT("use")) { Use(); }
	else if (Op == TEXT("status"))
	{
		UE_LOG(LogTemp, Log, TEXT("Person: %s  speed %.2f m/s  alt %.2f m  g %.2f m/s2  boots %s  jump peak %.2f m  pos %s"), *FrameDescription(), Velocity.Size() / 100.0, AltitudeCm / 100.0, GravityCmS2 / 100.0, bBootsArmed ? TEXT("armed") : TEXT("off"), LastJumpHeightCm / 100.0, *GetActorLocation().ToString());
	}
}
