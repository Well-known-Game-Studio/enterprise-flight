#include "PersonPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "CrewFigure.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Misc/Paths.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UDynamicMesh.h"
#include "MeshPrims.h"
#include "PlanetBody.h"
#include "RcsThrusterComponent.h"
#include "SlonkMaterials.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "UDynamicMesh.h"

namespace
{
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

	// The mannequin stands with its feet at the capsule's foot, facing the capsule's +X (the asset faces +Y).
	Figure = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Figure"));
	Figure->SetupAttachment(Body);
	Figure->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -HalfHeightCm), FRotator(0.f, -90.f, 0.f));
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->SetGenerateOverlapEvents(false);
	Figure->SetCanEverAffectNavigation(false);
	Figure->CastShadow = true;
	Figure->bAffectDistanceFieldLighting = false;
	Figure->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Figure->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// The Game Animation Sample's set on its UEFN mannequin: the loops in a blend space of our own, the one-shots by name.
	const FString A = TEXT("/Game/Characters/UEFN_Mannequin/Animations/");
	auto Seq = [&](const FString& Rel) { const FString N = FPaths::GetBaseFilename(Rel); return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(A + Rel + TEXT(".") + N)); };
	FigureMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(SlonkCrew::UEFNPath));
	FigureBaseMap = TSoftObjectPtr<UTexture>(FSoftObjectPath(SlonkCrew::UEFNBasePath));
	FigureNormalMap = TSoftObjectPtr<UTexture>(FSoftObjectPath(SlonkCrew::UEFNNormalPath));
	LocomotionBlend = TSoftObjectPtr<UBlendSpace>(FSoftObjectPath(A + TEXT("BS_Crew_Locomotion2D.BS_Crew_Locomotion2D")));
	JumpAnim = Seq(TEXT("Jump/M_Neutral_Jump_F_Start_Stand_Lfoot"));
	FallAnim = Seq(TEXT("Jump/M_Neutral_Jump_Loop_Fall"));
	LandAnim = Seq(TEXT("Jump/M_Neutral_Jump_F_Land_Stand_Light_Lfoot"));
	LandMovingAnim = Seq(TEXT("Jump/M_Neutral_Jump_F_Land_Walk_Light_Lfoot"));
	for (int32 i = 1; i <= 6; ++i) { IdleBreaks.Add(Seq(FString::Printf(TEXT("Idle/M_Neutral_Stand_Idle_Break_v%02d"), i))); }
	for (const TCHAR* Deg : { TEXT("045"), TEXT("090"), TEXT("135"), TEXT("180") })
	{
		TurnAnims.Add(Seq(FString::Printf(TEXT("Idle/M_Neutral_Stand_Turn_%s_L"), Deg)));
		TurnAnims.Add(Seq(FString::Printf(TEXT("Idle/M_Neutral_Stand_Turn_%s_R"), Deg)));
	}
	WalkStartAnim = Seq(TEXT("Walk/M_Neutral_Walk_Start_F_Lfoot")); RunStartAnim = Seq(TEXT("Run/M_Neutral_Run_Start_F_Lfoot"));
	WalkStopAnim = Seq(TEXT("Walk/M_Neutral_Walk_Stop_F_Lfoot")); RunStopAnim = Seq(TEXT("Run/M_Neutral_Run_Stop_F_Lfoot"));
	CrewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Crew.M_Crew")));
	Helmet = MakePart(this, TEXT("Helmet"), Body, FVector(0.f, 0.f, HeadHeightCm - HalfHeightCm));
	Hair = MakePart(this, TEXT("Hair"), Body, FVector(0.f, 0.f, HeadHeightCm - HalfHeightCm));
	for (TObjectPtr<UPointLightComponent>* L : { &BootLightL, &BootLightR })
	{
		UPointLightComponent* P = CreateDefaultSubobject<UPointLightComponent>(L == &BootLightL ? TEXT("BootLightL") : TEXT("BootLightR"));
		P->SetupAttachment(Body);
		P->SetRelativeLocation(FVector(0.f, L == &BootLightL ? -12.f : 12.f, -HalfHeightCm + 6.f));
		P->SetIntensityUnits(ELightUnits::Candelas);
		P->SetIntensity(0.f);
		P->SetLightColor(BootLightColor);
		P->SetAttenuationRadius(200.f);
		P->SetSourceRadius(5.f);
		P->SetCastShadows(false);
		P->bAffectsWorld = true;
		*L = P;
	}

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
	// The mannequin, one crew material instance per slot carrying that slot's own maps (head and legs, torso).
	if (USkeletalMesh* Mesh = FigureMesh.LoadSynchronous())
	{
		Figure->SetSkeletalMesh(Mesh);
		if (UMaterialInterface* Crew = CrewMaterial.LoadSynchronous()) { SlonkCrew::Dress(Figure, this, Crew, CrewMIDs, FigureBaseMap.LoadSynchronous(), FigureNormalMap.LoadSynchronous(), nullptr); }
		// The loops' own pace, so the blend space's speeds can be matched to them (see the log). The blend space was
		// authored through the editor's property tools, which do not rebuild its blend grid: in the editor, make sure.
		if (UBlendSpace* BS = LocomotionBlend.LoadSynchronous())
		{
#if WITH_EDITOR
			// The grid is serialized with the asset and the property tools leave it as the clone's: rebuild it here, and
			// the next save of the asset keeps it (ValidateSampleData alone only checks the samples).
			BS->ValidateSampleData();
			BS->ResampleData();
			BS->MarkPackageDirty();
#endif
			for (const FBlendSample& Sample : BS->GetBlendSamples())
			{
				if (const UAnimSequence* Seq = Cast<UAnimSequence>(Sample.Animation.Get()); Seq && Sample.SampleValue.Y > 0.f && FMath::IsNearlyZero(Sample.SampleValue.X))
				{
					const float L = Seq->GetPlayLength();
					UE_LOG(LogTemp, Log, TEXT("Person: %s runs %.0f cm/s by its root over %.1fs (placed at %.0f)"), *Seq->GetName(), L > 0.f ? (float)Seq->ExtractRootMotionFromRange(0.f, L, FAnimExtractContext(0.0, true)).GetTranslation().Size() / L : 0.f, L, Sample.SampleValue.Y);
				}
			}
		}
		if (UBlendSpace* BS = LocomotionBlend.LoadSynchronous())
		{
			Figure->PlayAnimation(BS, true);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Person: %s missing, no figure"), *FigureMesh.ToString());
	}

	// The helmet: a shell round the head with the visor let into its front (shell material 0, visor 1).
	UDynamicMesh* HM = NewObject<UDynamicMesh>(this);
	CurrentMaterialID() = 0;
	Ellipsoid(HM, FVector::ZeroVector, FVector(16.f, 16.f, 17.f), 16);
	CylinderZ(HM, FVector(0.f, 0.f, -17.f), 9.f, 5.f, 16);                    // neck ring
	CurrentMaterialID() = 1;
	Ellipsoid(HM, FVector(8.f, 0.f, 0.f), FVector(10.f, 12.f, 9.f), 14);       // visor
	FinishPart(Helmet, HM);
	Helmet->CastShadow = true;
	// A dark cap over the crown of the mannequin's head stands for hair when the helmet is off.
	UDynamicMesh* HairM = NewObject<UDynamicMesh>(this);
	CurrentMaterialID() = 0;
	Ellipsoid(HairM, FVector(-1.f, 0.f, 3.f), FVector(12.5f, 12.f, 8.5f), 14);
	FinishPart(Hair, HairM);

	if (UMaterialInterface* Hull = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::HullPath)), TEXT("Hull"), true))
	{
		TrimMID = UMaterialInstanceDynamic::Create(Hull, this);
		TrimMID->SetVectorParameterValue(TEXT("HullColor"), SuitTrim);
		TrimMID->SetScalarParameterValue(TEXT("PanelScale"), 3.f);
		TrimMID->SetScalarParameterValue(TEXT("Roughness"), 0.5f);
		TrimMID->SetScalarParameterValue(TEXT("Metallic"), 0.f);
		HairMID = UMaterialInstanceDynamic::Create(Hull, this);
		HairMID->SetVectorParameterValue(TEXT("HullColor"), HairColor);
		HairMID->SetScalarParameterValue(TEXT("PanelScale"), 3.f);
		HairMID->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
		HairMID->SetScalarParameterValue(TEXT("Metallic"), 0.f);
	}
	if (UMaterialInterface* Glow = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("Glow"), false))
	{
		VisorMID = UMaterialInstanceDynamic::Create(Glow, this);
		VisorMID->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(1.f, 0.82f, 0.45f));
		VisorMID->SetScalarParameterValue(TEXT("Intensity"), 1.2f);
	}
	StandYaw = GetActorRotation().Yaw;
	LastFigureLocation = GetActorLocation();
	bSuitWorn = !WearsSuit();   // so the first ApplyUniform dresses the figure
	ApplyUniform();
}

void APersonPawn::ApplyUniform()
{
	const bool bSuit = WearsSuit();
	// The suit is one colour from the boots up with dark trim at the hands and boots; the uniform is the jacket
	// over black trousers, the head and hands bare.
	FCrewDress D = bSuit ? FCrewDress::Suit(SuitColor, SuitTrim) : FCrewDress::Uniform(UniformTorso, SkinColor);
	D.Legs = bSuit ? SuitColor : UniformLegs;
	SlonkCrew::SetDress(CrewMIDs, D);
	// Mid-beam (the suit goes on as the figure lands) the helmet keeps the shimmer's glow; its own materials come
	// back when the transporter lets go.
	if (bTransportGlow && TransportMID && Helmet) { Helmet->ConfigureMaterialSet({ TransportMID.Get(), TransportMID.Get() }); }
	else if (TrimMID && VisorMID && Helmet) { Helmet->ConfigureMaterialSet({ TrimMID.Get(), VisorMID.Get() }); }
	if (bTransportGlow && TransportMID && Hair) { Hair->ConfigureMaterialSet({ TransportMID.Get() }); }
	else if (HairMID && Hair) { Hair->ConfigureMaterialSet({ HairMID.Get() }); }
	bSuitWorn = bSuit;
	ApplyFigureVisibility();
}

void APersonPawn::ApplyFigureVisibility()
{
	if (Figure) { Figure->SetVisibility(bFigureShown); }
	// The dissolve takes the figure from the feet up; the helmet goes last.
	for (UMaterialInstanceDynamic* Mid : CrewMIDs) { if (Mid) { Mid->SetScalarParameterValue(TEXT("Dissolve"), TransportDissolve); } }
	if (Helmet) { Helmet->SetVisibility(bFigureShown && bSuitWorn && TransportDissolve <= 0.8f); }
	if (Hair) { Hair->SetVisibility(bFigureShown && !bSuitWorn && TransportDissolve <= 0.8f); }
}

void APersonPawn::SetTransportState(float Glow, float Dissolve)
{
	const bool bWantGlow = Glow > 0.f;
	if (bWantGlow && !TransportMID)
	{
		if (UMaterialInterface* GlowMat = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("Glow"), false))
		{
			TransportMID = UMaterialInstanceDynamic::Create(GlowMat, this);
			TransportMID->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.35f, 0.62f, 1.f));
		}
	}
	if (bWantGlow != bTransportGlow)
	{
		bTransportGlow = bWantGlow;
		if (bWantGlow && TransportMID)
		{
			if (Helmet) { Helmet->ConfigureMaterialSet({ TransportMID.Get(), TransportMID.Get() }); }
			if (Hair) { Hair->ConfigureMaterialSet({ TransportMID.Get() }); }
		}
		else
		{
			if (TrimMID && VisorMID && Helmet) { Helmet->ConfigureMaterialSet({ TrimMID.Get(), VisorMID.Get() }); }
			if (HairMID && Hair) { Hair->ConfigureMaterialSet({ HairMID.Get() }); }
		}
	}
	if (bTransportGlow && TransportMID) { TransportMID->SetScalarParameterValue(TEXT("Intensity"), 1.5f + 9.f * FMath::Clamp(Glow, 0.f, 1.f)); }
	for (UMaterialInstanceDynamic* Mid : CrewMIDs) { if (Mid) { Mid->SetScalarParameterValue(TEXT("Glow"), FMath::Clamp(Glow, 0.f, 1.f)); } }
	TransportDissolve = FMath::Clamp(Dissolve, 0.f, 1.f);
	ApplyFigureVisibility();
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
	// The carry starts from the world's turn now, not from a tick later: a frame of its spin is metres of ground.
	CarryPlanet = GroundPlanet;
	if (APlanetBody* G = GroundPlanet.Get()) { CarryPlanetQuat = G->GetActorQuat(); }
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
	// The floor under the next step: a kerb or a pad up to StepUpCm is taken, a drop found below.
	FHitResult Floor;
	if (TraceSolid(NextFeet + Up * StepUpCm, NextFeet - Up * 120.0, Floor)) { NextFeet = Floor.ImpactPoint; }
	else if (Vt.SizeSquared() > 1.0)
	{
		// No deck under the next step (an opening, a gap in the set): stop at the edge rather than walk out on air.
		Velocity = FVector::ZeroVector;
		NextFeet = Feet;
	}
	SetActorLocationAndRotation(NextFeet + Up * HalfHeightCm, StandingQuat(Up, Fwd), false, nullptr, ETeleportType::TeleportPhysics);
	bGrounded = true;
	// Through a doorway into the next room: the ship follows the feet and shows the room they are in. The craft-
	// frame position is refreshed first (Tick refreshes it after this), so the step that stops in a doorway counts.
	if (Vt.SizeSquared() > 1.0)
	{
		HullLocal = GetActorTransform().GetRelativeTransform(FTransform(C->GetActorQuat(), C->GetActorLocation()));
		C->PersonMoved(this);
	}
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
	ApplyFigureVisibility();
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

void APersonPawn::PlayFigure(EFigureAnim Anim, UAnimationAsset* Asset, float Seconds)
{
	if (!Figure) { return; }
	bool bLoop = false;
	if (!Asset)
	{
		switch (Anim)
		{
		case EFigureAnim::Jump: Asset = JumpAnim.LoadSynchronous(); Seconds = 0.55f; break;
		case EFigureAnim::Fall: Asset = FallAnim.LoadSynchronous(); bLoop = true; break;
		case EFigureAnim::Land: Asset = LandAnim.LoadSynchronous(); Seconds = 0.6f; break;
		default: Asset = LocomotionBlend.LoadSynchronous(); bLoop = true; break;
		}
	}
	if (!Asset) { Anim = EFigureAnim::Locomotion; Asset = LocomotionBlend.LoadSynchronous(); bLoop = true; }
	if (!Asset) { return; }
	FigureAnim = Anim;
	Figure->PlayAnimation(Asset, bLoop);
	OneShotLeft = bLoop ? 0.f : (Seconds > 0.f ? Seconds : FMath::Max(Asset->GetPlayLength() - 0.1f, 0.1f));
}

void APersonPawn::UpdateFigure(float DeltaTime)
{
	const FVector Up = GetActorUpVector();
	const FVector Vt = Velocity - Up * FVector::DotProduct(Velocity, Up);
	const float Speed = bGrounded ? (float)Vt.Size() : 0.f;
	Stride = FMath::Clamp(Speed / WalkSpeedCm, 0.f, 1.6f);
	Limber = FMath::Lerp(Limber, bGrounded ? 0.f : 1.f, 1.f - FMath::Exp(-4.f * DeltaTime));

	if (Figure && Figure->GetSkeletalMeshAsset())
	{
		const bool bAirborne = !bGrounded;
		const bool bOneShot = OneShotLeft > 0.f;
		const bool bMoving = Speed > 30.f;
		const bool bSprinting = Speed > WalkSpeedCm * 1.3f;
		const float ActorYaw = GetActorRotation().Yaw;
		// Placed somewhere new (a lift, the hatch, the transporter): the feet point where the body does.
		if (FVector::DistSquared(GetActorLocation(), LastFigureLocation) > 100.0 * 100.0) { StandYaw = ActorYaw; FigureYawOffset = 0.f; }
		LastFigureLocation = GetActorLocation();
		// Leaving the ground with a push: the jump; coming down onto it: a landing, lighter in stride; in the air
		// or adrift: the fall loop, limbs loose.
		if (bAirborne && bWasGrounded && FVector::DotProduct(Velocity, Up) > 60.0) { PlayFigure(EFigureAnim::Jump); }
		else if (!bAirborne && !bWasGrounded && (FigureAnim == EFigureAnim::Fall || FigureAnim == EFigureAnim::Jump))
		{
			PlayFigure(EFigureAnim::Land, bMoving ? LandMovingAnim.LoadSynchronous() : nullptr, 0.6f);
		}
		else if (bAirborne && !bOneShot && FigureAnim != EFigureAnim::Fall) { PlayFigure(EFigureAnim::Fall); }
		else if (!bAirborne)
		{
			// On the ground: a step into the loop when setting off, a step out of it when stopping; a break now and
			// then while standing; and when the body turns on the spot past the planted feet, a turn clip.
			if (bStepIntoWalk && bMoving && LastSpeed <= 30.f && (FigureAnim == EFigureAnim::Locomotion || FigureAnim == EFigureAnim::IdleBreak || FigureAnim == EFigureAnim::Turn || FigureAnim == EFigureAnim::Stop))
			{
				PlayFigure(EFigureAnim::Start, (bSprinting ? RunStartAnim : WalkStartAnim).LoadSynchronous(), StartClipSeconds);
			}
			else if (bStepIntoWalk && !bMoving && LastSpeed > 150.f && FigureAnim == EFigureAnim::Locomotion)
			{
				PlayFigure(EFigureAnim::Stop, (LastSpeed > WalkSpeedCm * 1.3f ? RunStopAnim : WalkStopAnim).LoadSynchronous(), StopClipSeconds);
			}
			else if (!bMoving && FigureAnim == EFigureAnim::Locomotion)
			{
				const float Turned = FMath::FindDeltaAngleDegrees(StandYaw, ActorYaw);
				if (FMath::Abs(Turned) > TurnInPlaceDeg && TurnAnims.Num() >= 8)
				{
					// The nearest of 45, 90, 135, 180 to the left or right; the feet come round with the clip.
					const int32 Step = FMath::Clamp(FMath::RoundToInt(FMath::Abs(Turned) / 45.f), 1, 4) - 1;
					if (UAnimSequence* T = TurnAnims[Step * 2 + (Turned < 0.f ? 0 : 1)].LoadSynchronous())
					{
						PlayFigure(EFigureAnim::Turn, T, FMath::Min(T->GetPlayLength() - 0.1f, 0.9f + 0.2f * Step));
						TurnSeconds = OneShotLeft;
						TurnUnwind = FigureYawOffset;
						StandYaw = ActorYaw;
					}
				}
				else
				{
					IdleSeconds += DeltaTime;
					if (IdleSeconds > NextIdleBreak && IdleBreaks.Num() > 0)
					{
						if (UAnimSequence* B = IdleBreaks[FMath::RandRange(0, IdleBreaks.Num() - 1)].LoadSynchronous()) { PlayFigure(EFigureAnim::IdleBreak, B, B->GetPlayLength() - 0.1f); }
						IdleSeconds = 0.f;
						NextIdleBreak = FMath::FRandRange((float)IdleBreakEvery.X, (float)IdleBreakEvery.Y);
					}
				}
			}
			if (bMoving) { IdleSeconds = 0.f; StandYaw = ActorYaw; }
		}
		if (OneShotLeft > 0.f)
		{
			OneShotLeft -= DeltaTime;
			// Moving cuts a break, a turn or a stop short; anything that ends goes back to the loop (or the fall).
			const bool bInterrupt = bMoving && (FigureAnim == EFigureAnim::IdleBreak || FigureAnim == EFigureAnim::Turn || FigureAnim == EFigureAnim::Stop);
			if (OneShotLeft <= 0.f || bInterrupt)
			{
				PlayFigure(bAirborne ? EFigureAnim::Fall : EFigureAnim::Locomotion);
				if (bStepIntoWalk && bInterrupt && bMoving) { PlayFigure(EFigureAnim::Start, (bSprinting ? RunStartAnim : WalkStartAnim).LoadSynchronous(), StartClipSeconds); }
			}
		}
		if (FigureAnim == EFigureAnim::Locomotion)
		{
			if (UAnimSingleNodeInstance* Node = Figure->GetSingleNodeInstance())
			{
				const FVector Fwd = GetActorForwardVector(), Right = GetActorRightVector();
				const float Dir = Speed > 5.f ? FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Vt, Right), FVector::DotProduct(Vt, Fwd))) : 0.f;
				Node->SetBlendSpacePosition(FVector(Dir, Speed, 0.f));
			}
		}
		// Standing, the figure keeps the feet where they were planted while the body (and the camera) turn, until a
		// turn clip brings them round; walking, it faces the way the body does.
		if (bMoving || bAirborne) { FigureYawOffset = 0.f; }
		else if (FigureAnim == EFigureAnim::Turn) { FigureYawOffset = TurnUnwind * FMath::Clamp(OneShotLeft / FMath::Max(TurnSeconds, 0.01f), 0.f, 1.f); }
		else { FigureYawOffset = FMath::FindDeltaAngleDegrees(ActorYaw, StandYaw); }
		Figure->SetRelativeRotation(FRotator(0.f, -90.f + FigureYawOffset, 0.f));
		bWasGrounded = bGrounded;
		LastSpeed = Speed;
		// The helmet and the hair cap ride the head bone's position, squared to the body so the visor looks where the person faces.
		if ((Helmet && Helmet->IsVisible()) || (Hair && Hair->IsVisible()))
		{
			const FVector Head = Figure->DoesSocketExist(TEXT("head")) ? Figure->GetSocketLocation(TEXT("head")) + Up * 9.0 : GetActorLocation() + Up * (HeadHeightCm - HalfHeightCm);
			const FQuat HeadQ = GetActorQuat() * FQuat(FVector::UpVector, FMath::DegreesToRadians(FigureYawOffset));
			if (Helmet) { Helmet->SetWorldLocationAndRotation(Head, HeadQ); }
			if (Hair) { Hair->SetWorldLocationAndRotation(Head, HeadQ); }
		}
	}
	if (bSuitWorn != WearsSuit()) { ApplyUniform(); }

	const bool bHolding = Frame == EPersonFrame::Hull && bGrounded;
	const bool bArmed = bBootsArmed && WearsSuit();
	for (UMaterialInstanceDynamic* Mid : CrewMIDs)
	{
		if (Mid) { Mid->SetScalarParameterValue(TEXT("BootGlow"), bHolding ? 10.f : (bArmed ? 1.5f : 0.f)); }
	}
	// The boot lamps ride the feet and light the hull round them while the boots hold.
	const float Lamp = bHolding ? BootLightHolding : (bArmed ? BootLightArmed : 0.f);
	for (const TPair<UPointLightComponent*, FName>& Foot : { TPair<UPointLightComponent*, FName>(BootLightL.Get(), TEXT("foot_l")), TPair<UPointLightComponent*, FName>(BootLightR.Get(), TEXT("foot_r")) })
	{
		if (!Foot.Key) { continue; }
		if (Figure && Figure->DoesSocketExist(Foot.Value)) { Foot.Key->SetWorldLocation(Figure->GetSocketLocation(Foot.Value) + Up * 5.0); }
		if (!FMath::IsNearlyEqual(Foot.Key->Intensity, Lamp)) { Foot.Key->SetIntensity(Lamp); Foot.Key->SetVisibility(Lamp > 0.f); }
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
			ApplyFigureVisibility();
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
	else if (Op == TEXT("face"))
	{
		// face <yaw>: turn at once to a bearing in the craft's frame while on its hull or decks (0 = the bow; HullLocal is
		// kept in that frame, whatever the ship is doing), the world's otherwise.
		const bool bAboard = Frame == EPersonFrame::Hull || Frame == EPersonFrame::Deck;
		const FVector F = bAboard ? HullLocal.GetRotation().GetForwardVector() : GetActorForwardVector();
		LookDelta.X += FMath::FindDeltaAngleDegrees(FMath::RadiansToDegrees(FMath::Atan2(F.Y, F.X)), Arg(1, 0.f));
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
		UE_LOG(LogTemp, Log, TEXT("Person: %s  speed %.2f m/s  alt %.2f m  g %.2f m/s2  boots %s  jump peak %.2f m  pos %s  feet in craft %s"), *FrameDescription(), Velocity.Size() / 100.0, AltitudeCm / 100.0, GravityCmS2 / 100.0, bBootsArmed ? TEXT("armed") : TEXT("off"), LastJumpHeightCm / 100.0, *GetActorLocation().ToString(), (Frame == EPersonFrame::Hull || Frame == EPersonFrame::Deck) ? *FeetInCraft().ToString() : TEXT("-"));
	}
}
