// Slonk - a leviathan alongside: its body in space, its station on the ship's beam, and what it shows.

#include "Leviathan.h"
#include "Missions.h"
#include "StarshipPawn.h"
#include "ShipSystemsComponent.h"
#include "SlonkGameMode.h"
#include "FloatingOriginComponent.h"
#include "TractorBeamComponent.h"
#include "WarpDriveComponent.h"
#include "RockMeshes.h"
#include "LandmarkMeshes.h"
#include "SlonkMaterials.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The body in the head's own measure (the rock meshes are 50 cm in radius along their length, as the engine sphere
	// is, and narrower across it; the head's scale is its radius over that): each rock of the tail a little smaller
	// than the one before, laid end to end along the body and sunk well into its neighbours, with a light in every seam.
	constexpr float LevUnit = 50.f;
	constexpr float LevHeadRadiusCm = 45000.f;   // nine hundred metres across
	float LevSegmentScale(int32 i) { return i <= 0 ? 1.f : FMath::Lerp(0.95f, 0.4f, (i - 1) / (float)(ALeviathan::NumSegments - 1)); }
	constexpr float LevOverlap = 0.72f;
	/** How far down the body the i-th rock's centre lies, in the head's measure (0 is the head). */
	float LevSegmentAlong(int32 i)
	{
		float X = 0.f;
		for (int32 k = 1; k <= i; ++k) { X += LevOverlap * LevUnit * (LevSegmentScale(k - 1) + LevSegmentScale(k)); }
		return X;
	}
	/** The ship takes no hurt from flying through it, and no camera is pushed off it; her phasers' trace and her
	 *  torpedoes' sweep meet it all the same. */
	void LevCollision(UStaticMeshComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionObjectType(ECC_WorldDynamic);
		C->SetCollisionResponseToAllChannels(ECR_Block);
		C->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		C->SetGenerateOverlapEvents(false);
	}
	/** Seconds a lash lasts, and how far ahead of the ship's beam the head lies once it is on station, cm. */
	constexpr float LevLashSeconds = 2.5f;
	constexpr double LevLeadCm = 2.0e5;
	/** How bright a collar burns for a light of one (the glow material's own measure). */
	constexpr float LevGlow = 20.f;
}

ALeviathan::ALeviathan()
{
	// Placed from the ship once she has moved this frame (her movement ticks before physics) and the origin has
	// shifted (during it; Spawn orders the two): so it keeps her course and speed to the metre, and everything that
	// looks at it after physics (her helm, her beam, her phasers, the HUD) finds it where it is, not a frame of her own
	// travel astern, which at impulse is more than a thousand kilometres. (A torpedo moves and seeks before physics, in
	// the world's own frame, as at anything that keeps pace with a ship under way: it is sure only from a ship at rest.)
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	Radius = LevHeadRadiusCm; TumbleRate = FRotator::ZeroRotator;
	MaxHealth = DriveOffDamage; Health = DriveOffDamage;
	LevCollision(Body);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 1; i <= NumSegments; ++i)
	{
		UStaticMeshComponent* Seg = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Segment%02d"), i));
		Seg->SetupAttachment(Body);
		Seg->SetMobility(EComponentMobility::Movable);
		LevCollision(Seg);
		if (SphereFinder.Succeeded()) { Seg->SetStaticMesh(SphereFinder.Object); }
		Seg->SetRelativeLocation(FVector(-LevSegmentAlong(i), 0.f, 0.f));
		Seg->SetRelativeScale3D(FVector(LevSegmentScale(i)));
		Segments.Add(Seg);
		UStaticMeshComponent* Light = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Light%02d"), i));
		Light->SetupAttachment(Body);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Light->SetGenerateOverlapEvents(false);
		Light->SetCastShadow(false);
		if (SphereFinder.Succeeded()) { Light->SetStaticMesh(SphereFinder.Object); }
		Light->SetRelativeLocation(FVector(-(LevSegmentAlong(i - 1) + LevSegmentAlong(i)) * 0.5f, 0.f, 0.f));
		// (A collar: thin along the body, and wide enough to stand a little proud of the rocks it lies between.)
		Light->SetRelativeScale3D(FVector(0.1f, 0.62f, 0.62f) * LevSegmentScale(i));
		Lights.Add(Light);
		if (i % 2 == 1)
		{
			UPointLightComponent* Lamp = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("Lamp%02d"), i));
			Lamp->SetupAttachment(Body);
			Lamp->SetMobility(EComponentMobility::Movable);
			Lamp->SetUsingAbsoluteScale(true);
			Lamp->SetIntensityUnits(ELightUnits::Candelas);
			// (So bright that the flank some hundreds of metres off it is lit as a phaser's strike lights a hull.)
			const float Size = LevSegmentScale(i);
			Lamp->SetIntensity(6.0e6f * Size * Size);
			Lamp->SetAttenuationRadius(LevHeadRadiusCm * 3.f * Size);
			Lamp->SetLightColor(ALeviathan::Pale());
			Lamp->CastShadows = false;
			Lamps.Add(Lamp);
		}
	}
	Stone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stone"));
	Stone->SetupAttachment(Body);
	Stone->SetMobility(EComponentMobility::Movable);
	Stone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stone->SetGenerateOverlapEvents(false);
}

ALeviathan* ALeviathan::Spawn(UWorld* W, AStarshipPawn* InShip, const FString& Name, int32 InMissionId, double HoursLeft)
{
	if (!W || !InShip) { return nullptr; }
	const FVector Side = InShip->GetActorRightVector(), Ahead = InShip->GetActorForwardVector();
	const double Off = StationCm(HoursLeft), From = -6.0e5;   // it comes up from astern
	const FTransform At(FRotationMatrix::MakeFromXY(Ahead, -Side).ToQuat(), InShip->GetActorLocation() + Side * Off + Ahead * From);
	ALeviathan* A = W->SpawnActorDeferred<ALeviathan>(ALeviathan::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!A) { return nullptr; }
	A->DisplayName = Name; A->MissionId = InMissionId; A->Ship = InShip;
	A->Bearing = Side; A->Heading = Ahead; A->Range = Off; A->Lead = From;
	A->FinishSpawning(At);
	// (After the origin's shift, which moves the ship and everything else in the same tick group.)
	if (UFloatingOriginComponent* Origin = ASlonkGameMode::FindFloatingOrigin(W)) { A->AddTickPrerequisiteComponent(Origin); }
	UE_LOG(LogTemp, Log, TEXT("Leviathan %s: on the starboard beam at %.1f km, for mission %d"), *Name, Off / 1.0e5, InMissionId);
	return A;
}

ALeviathan* ALeviathan::FindByMission(UWorld* World, int32 InMissionId)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ALeviathan> It(World); It; ++It) { if (It->MissionId == InMissionId) { return *It; } }
	return nullptr;
}

void ALeviathan::BeginPlay()
{
	Super::BeginPlay();   // the head: a rock in the asteroid material
	// (The same creature has the same body whenever it is met, a return after it was left astern included: its rocks
	// are drawn from its name, the head's too.)
	FRandomStream R((int32)GetTypeHash(DisplayName));
	if (UStaticMesh* Head = SlonkRocks::GetRockMesh(R.RandRange(0, SlonkRocks::NumVariants - 1))) { Body->SetStaticMesh(Head); }
	UMaterialInterface* Rock = Body->GetMaterial(0);
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		UStaticMeshComponent* Seg = Segments[i];
		if (UStaticMesh* Mesh = SlonkRocks::GetRockMesh(R.RandRange(0, SlonkRocks::NumVariants - 1))) { Seg->SetStaticMesh(Mesh); }
		if (Rock) { Seg->SetMaterial(0, Rock); }
		// (Its length along the body, either end first, rolled any way about it.)
		Seg->SetRelativeRotation(FRotator(R.FRandRange(-8.f, 8.f), (R.FRand() < 0.5f ? 0.f : 180.f) + R.FRandRange(-8.f, 8.f), R.FRandRange(-180.f, 180.f)));
	}
	if (UMaterialInterface* Glow = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("LeviathanGlow")))
	{
		for (UStaticMeshComponent* Light : Lights)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glow, this);
			if (M) { M->SetVectorParameterValue(TEXT("GlowColor"), Pale()); M->SetScalarParameterValue(TEXT("Intensity"), 0.45f * LevGlow); Light->SetMaterial(0, M); }
			LightMats.Add(M);
		}
	}
	// The road's stone, set in the head's case on the side it shows the ship: a standing stone four hundred metres
	// tall, its foot sunk in the rock.
	if (UStaticMesh* Mesh = SlonkLandmarks::GetMesh(ELandmark::Monolith))
	{
		Stone->SetStaticMesh(Mesh);
		// (The same rock as its case, dressed smooth and pale: a made thing among the rough.)
		if (UMaterialInstanceDynamic* Dressed = Rock ? UMaterialInstanceDynamic::Create(Rock, this) : nullptr)
		{
			Dressed->SetScalarParameterValue(TEXT("NormalStrength"), 0.f);
			for (const TCHAR* Tone : { TEXT("RockColorA"), TEXT("RockColorB"), TEXT("RockColorC") }) { Dressed->SetVectorParameterValue(Tone, FLinearColor(0.5f, 0.49f, 0.45f)); }
			Stone->SetMaterial(0, Dressed);
		}
		const float Tall = 40000.f / (float)SlonkLandmarks::UnitCm;   // the mesh's own measure is its height
		const FVector Out = FVector(0.f, 0.6f, -0.8f);   // toward her and up by her own reckoning (its own up is her down), so she sees it stand off the head
		Stone->SetRelativeScale3D(FVector(Tall / (LevHeadRadiusCm / LevUnit)));
		Stone->SetRelativeLocation(Out * 28.f);
		Stone->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector, Out));
	}
	else { Stone->SetVisibility(false); }
	Swim(0.f);
}

void ALeviathan::Ripple(const FLinearColor& Color) { RippleColor = Color; RippleAge = 0.f; }

void ALeviathan::Lash() { LashAge = 0.f; }

void ALeviathan::Depart(bool bInSettled)
{
	if (bDeparting) { return; }
	bDeparting = true; bSettled = bInSettled; DepartAge = 0.f;
	Tags.Remove(TargetableTag);   // her guns and her beam let it go
}

float ALeviathan::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Only the ship's own guns are the captain's choice: a shot the player's controller is behind, or, with nobody
	// behind it, one her own hull caused (her phasers name her) or owns (her torpedoes). A cube's torpedo that bursts
	// on the creature, or a stray, is not hers. And nothing more is to be done to one that is going.
	const bool bHers = EventInstigator ? EventInstigator->IsPlayerController()
		: (Cast<AStarshipPawn>(DamageCauser) != nullptr || (DamageCauser && Cast<AStarshipPawn>(DamageCauser->GetOwner()) != nullptr));
	if (bDeparting || DamageAmount <= 0.f || !bHers) { return 0.f; }
	const float Applied = AActor::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	UMissionSubsystem* Ms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!Ms || Applied <= 0.f) { return Applied; }
	if (!bFiredOn) { bFiredOn = true; Ms->LeviathanOutcome(MissionId, ELeviathanOutcome::Struck); }
	DamageTaken += Applied;
	Health = FMath::Max(MaxHealth - DamageTaken, 1.f);   // it is turned, never killed
	if (DamageTaken >= DriveOffDamage) { Ms->LeviathanOutcome(MissionId, ELeviathanOutcome::Driven); }
	return Applied;
}

void ALeviathan::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	const UMissionSubsystem* Ms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr;
	const FMission* M = Ms ? Ms->Find(MissionId) : nullptr;
	AStarshipPawn* S = Ship.Get();
	// Its mission closed with nothing said to it (the ship gone to warp, the board reset): it turns back to its road.
	if (!bDeparting && (!M || M->State != EMissionState::Active || !S)) { Depart(false); }
	const bool bLashing = LashAge >= 0.f;
	if (bLashing) { LashAge += DeltaTime; if (LashAge >= LevLashSeconds) { LashAge = -1.f; } }
	if (bDeparting && !bLashing)
	{
		DepartAge += DeltaTime;
		if (DepartAge > 60.f) { Destroy(); return; }
		// Off down its road, ahead and away from her, gathering way, and gone in a minute: as the ship sees it while her
		// drive is idle (a ship under way at impulse would else leave it a body's length astern in a frame), and on its
		// own in the world from the moment she goes to warp, or is gone. Left astern, it is the ship that goes: it
		// stays where it was in the world while she draws away, and only then turns for its road (so one left by a
		// ship that has gone down into air, and not far, is not seen to hang there and vanish).
		const bool bLeft = M && M->Exit == FName(TEXT("Slipped"));
		const float Going = DepartAge - (bLeft ? 4.f : 0.f);
		bAdrift |= bLeft;
		if (Going > 0.f)
		{
			const double Speed = FMath::Min((double)Going * 5.0e4, 8.0e5);
			const FVector Step = (Heading * 0.9 + Bearing * 0.45).GetSafeNormal() * Speed * DeltaTime;
			bAdrift |= !S || (S->WarpDrive && S->WarpDrive->IsWarpEngaged());
			if (bAdrift) { SetActorLocation(GetActorLocation() + Step, false, nullptr, ETeleportType::TeleportPhysics); }
			else { DepartOff += Step; SetActorLocation(S->GetActorLocation() + Bearing * Range + Heading * Lead + DepartOff, false, nullptr, ETeleportType::TeleportPhysics); }
		}
	}
	else if (S)
	{
		// A beam finds nothing it can hold: the ship's, or her shuttle's.
		BeamCooldown -= DeltaTime;
		for (TActorIterator<AStarshipPawn> It(GetWorld()); It && !bDeparting; ++It)
		{
			if (!(It->TractorBeam && It->TractorBeam->IsBeamActive() && It->TractorBeam->Target == this)) { continue; }
			It->TractorBeam->Release();
			Ripple(Blue());
			if (BeamCooldown <= 0.f && S->Systems) { BeamCooldown = 4.f; S->Systems->Report(EStation::Engineering, TEXT("The beam will not take hold, Captain: there is too much of it."), FLinearColor(1.f, 0.75f, 0.5f)); }
		}
		// Its station: so far off her beam as the mission's hours say, nearer with every one spent; its length across
		// the line between them, turning as she turns; its head a little ahead of her.
		if (M && !bDeparting) { Range = FMath::FInterpConstantTo((float)Range, (float)StationCm(Ms->PassHoursLeft(*M)), DeltaTime, 3.0e4f); }
		Lead = FMath::FInterpConstantTo((float)Lead, (float)LevLeadCm, DeltaTime, 1.0e5f);
		FVector Fwd = S->GetActorForwardVector(); Fwd -= Bearing * FVector::DotProduct(Fwd, Bearing);
		if (Fwd.SizeSquared() > 0.04)
		{
			Heading = FMath::VInterpNormalRotationTo(Heading, Fwd.GetSafeNormal(), DeltaTime, 12.f);
			Heading -= Bearing * FVector::DotProduct(Heading, Bearing);
			if (!Heading.Normalize()) { Heading = Fwd.GetSafeNormal(); }
		}
		// A lash: in along the hull and out again.
		double Off = Range;
		if (LashAge >= 0.f) { Off = FMath::Lerp(Range, FMath::Min(Range, 6.0e4), (double)FMath::Sin(PI * LashAge / LevLashSeconds)); }   // (not on the frame it ends)
		SetActorLocationAndRotation(S->GetActorLocation() + Bearing * Off + Heading * Lead, FRotationMatrix::MakeFromXY(Heading, -Bearing).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
	}
	Swim(DeltaTime);
}

void ALeviathan::Swim(float DeltaTime)
{
	// A slow wave down the body, the wider toward the tail; the lights in the seams ride it.
	const float Phase = Age * 0.35f;
	TArray<FVector, TInlineAllocator<NumSegments + 1>> At; At.Add(FVector::ZeroVector);
	for (int32 i = 1; i <= Segments.Num(); ++i)
	{
		const float A = 1.5f + 0.9f * i, P = Phase - 0.45f * i;
		const FVector L(-LevSegmentAlong(i), A * 0.25f * FMath::Cos(P), A * FMath::Sin(P));
		Segments[i - 1]->SetRelativeLocation(L);
		At.Add(L);
	}
	// The lights: pale and slowly breathing; a ripple is a band of another colour run down them; a lash is white
	// from end to end; one that has had what it came for goes with its lights low.
	if (RippleAge >= 0.f) { RippleAge += DeltaTime; if (RippleAge > 4.f) { RippleAge = -1.f; } }
	Dim = FMath::FInterpTo(Dim, bDeparting && bSettled ? 0.2f : 1.f, DeltaTime, 0.5f);
	const float White = LashAge >= 0.f ? 1.f - LashAge / LevLashSeconds : 0.f;
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		const FVector Seam = (At[i] + At[i + 1]) * 0.5f;
		Lights[i]->SetRelativeLocation(Seam);
		FLinearColor C = Pale() * (0.45f + 0.12f * FMath::Sin(Age * 0.8f - 0.9f * i)) * Dim;
		if (RippleAge >= 0.f) { const float D = i - RippleAge * 6.f; C = FMath::Lerp(C, RippleColor * 2.5f, FMath::Exp(-D * D * 0.5f)); }
		if (White > 0.f) { C = FMath::Lerp(C, FLinearColor::White * 4.f, White); }
		const float Bright = FMath::Max3(C.R, C.G, C.B);
		const FLinearColor Hue = Bright > 1.0e-3f ? C * (1.f / Bright) : Pale();
		if (LightMats.IsValidIndex(i) && LightMats[i]) { LightMats[i]->SetVectorParameterValue(TEXT("GlowColor"), Hue); LightMats[i]->SetScalarParameterValue(TEXT("Intensity"), Bright * LevGlow); }
		// The lamp of the seam, off the flank it shows the ship: as bright and of the colour its collar is.
		if (i % 2 == 0 && Lamps.IsValidIndex(i / 2) && Lamps[i / 2])
		{
			UPointLightComponent* Lamp = Lamps[i / 2];
			const float Size = LevSegmentScale(i + 1);
			Lamp->SetRelativeLocation(Seam + FVector(0.f, 1.5f * LevUnit * Size, 0.f));
			Lamp->SetLightColor(Hue);
			Lamp->SetIntensity(6.0e6f * Size * Size * Bright / 0.45f);
		}
	}
}
