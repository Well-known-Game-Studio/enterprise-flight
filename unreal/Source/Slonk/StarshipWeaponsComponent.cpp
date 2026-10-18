#include "StarshipWeaponsComponent.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PhaserBeam.h"
#include "PhotonTorpedo.h"
#include "SpaceTarget.h"
#include "StarshipMovementComponent.h"

UStarshipWeaponsComponent::UStarshipWeaponsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	PhaserBeamClass = APhaserBeam::StaticClass();
	TorpedoClass = APhotonTorpedo::StaticClass();

	// Defaults for a 305 m Constitution-refit hull centred on the root; the pawn lays the banks out on its mesh.
	PhaserBanks = { FPhaserBank(TEXT("DorsalFwd"), FVector(10600.0, 0.0, 2000.0), FVector::UpVector), FPhaserBank(TEXT("VentralFwd"), FVector(10600.0, 0.0, -600.0), FVector::DownVector) };
	// One launcher with two tubes side by side, at the foot of the neck directly above the deflector dish, facing
	// forward. The mouths sit in the two recessed door panes of the refit mesh (|y| 2.8 m, z -3.2 m); the pawn
	// overwrites these from its measured TorpedoTubeCentre when it lays out the hull.
	TorpedoTubeOffsets = { FVector(3170.0, -281.0, -323.0), FVector(3170.0, 281.0, -323.0) };
}

void UStarshipWeaponsComponent::BeginPlay()
{
	Super::BeginPlay();
	PhaserEnergy = 1.f;
}

void UStarshipWeaponsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyBeams();
	Super::EndPlay(EndPlayReason);
}

bool UStarshipWeaponsComponent::GetViewRay(FVector& OutOrigin, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn)
	{
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			FRotator ViewRot;
			PC->GetPlayerViewPoint(OutOrigin, ViewRot);
			OutDirection = ViewRot.Vector();
			return true;
		}
	}
	if (GetOwner())
	{
		OutOrigin = GetOwner()->GetActorLocation();
		OutDirection = GetOwner()->GetActorForwardVector();
		return true;
	}
	return false;
}

FVector UStarshipWeaponsComponent::GetAimPoint() const
{
	if (LockedTarget && IsValid(LockedTarget))
	{
		return LockedTarget->GetActorLocation();
	}
	FVector Origin, Dir;
	if (GetViewRay(Origin, Dir))
	{
		// Aim through the centre of the screen: trace from the view origin so the reticle is honest.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PhaserAim), false);
		Params.AddIgnoredActor(GetOwner());
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Dir * PhaserRange * 2.0, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
		return Origin + Dir * PhaserRange * 2.0;
	}
	return FVector::ZeroVector;
}

void UStarshipWeaponsComponent::StartPhasers()
{
	bPhasersFiring = true;
}

void UStarshipWeaponsComponent::StopPhasers()
{
	bPhasersFiring = false;
	DestroyBeams();
}

void UStarshipWeaponsComponent::DestroyBeams()
{
	for (APhaserBeam* Beam : ActiveBeams)
	{
		if (Beam)
		{
			Beam->Destroy();
		}
	}
	ActiveBeams.Reset();
}

void UStarshipWeaponsComponent::UpdatePhasers(float DeltaTime)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const bool bWantFire = bPhasersFiring && PhaserEnergy > 0.f;
	if (!bWantFire)
	{
		FiringBanks.Reset();
		if (ActiveBeams.Num() > 0)
		{
			DestroyBeams();
		}
		const float Recharge = PhaserRechargeSeconds > 0.f ? DeltaTime / PhaserRechargeSeconds : 1.f;
		PhaserEnergy = FMath::Min(1.f, PhaserEnergy + Recharge);
		return;
	}

	const FVector AimPoint = GetAimPoint();
	const FTransform ShipTransform = Owner->GetActorTransform();

	// Rank the banks by how squarely they face the aim point. A bank looking the other way cannot bear at all,
	// so a target astern is engaged from the stern banks and one below from the ventral saucer banks.
	struct FChoice { int32 Bank; double Cos; FVector Start; FVector Dir; };
	TArray<FChoice> Choices;
	const double MinCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(PhaserBankArc, 1.f, 179.f)));
	for (int32 i = 0; i < PhaserBanks.Num(); ++i)
	{
		const FVector Start = ShipTransform.TransformPosition(PhaserBanks[i].Offset);
		const FVector Dir = (AimPoint - Start).GetSafeNormal();
		const FVector Normal = ShipTransform.TransformVectorNoScale(PhaserBanks[i].Normal).GetSafeNormal();
		const double Cos = FVector::DotProduct(Dir, Normal);
		if (Cos >= MinCos)
		{
			Choices.Add({ i, Cos, Start, Dir });
		}
	}
	Choices.Sort([](const FChoice& A, const FChoice& B) { return A.Cos > B.Cos; });
	const int32 NumFiring = FMath::Min(Choices.Num(), FMath::Max(MaxFiringBanks, 1));

	// Spawn beams lazily, one per firing slot.
	while (ActiveBeams.Num() < NumFiring)
	{
		FActorSpawnParameters Params;
		Params.Owner = Owner;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APhaserBeam* Beam = World->SpawnActor<APhaserBeam>(PhaserBeamClass ? *PhaserBeamClass : APhaserBeam::StaticClass(), ShipTransform, Params);
		if (!Beam)
		{
			break;
		}
		ActiveBeams.Add(Beam);
	}

	FiringBanks.Reset();
	bool bAnyFired = false;
	for (int32 Slot = 0; Slot < ActiveBeams.Num(); ++Slot)
	{
		APhaserBeam* Beam = ActiveBeams[Slot];
		if (!Beam)
		{
			continue;
		}
		if (Slot >= NumFiring)
		{
			Beam->SetActorHiddenInGame(true);
			continue;
		}
		const FChoice& C = Choices[Slot];
		Beam->SetActorHiddenInGame(false);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PhaserBeam), false);
		Params.AddIgnoredActor(Owner);
		const FVector TraceEnd = C.Start + C.Dir * PhaserRange;
		const bool bHit = World->LineTraceSingleByChannel(Hit, C.Start, TraceEnd, ECC_Visibility, Params);
		const FVector End = bHit ? Hit.ImpactPoint : TraceEnd;
		Beam->SetBeam(C.Start, End, bHit);
		FiringBanks.Add(C.Bank);
		bAnyFired = true;

		if (bHit && Hit.GetActor())
		{
			AController* Instigator = Cast<APawn>(Owner) ? Cast<APawn>(Owner)->GetController() : nullptr;
			UGameplayStatics::ApplyPointDamage(Hit.GetActor(), PhaserDamagePerSecond * DeltaTime, C.Dir, Hit, Instigator, Owner, nullptr);
		}
	}

	if (bAnyFired)
	{
		const float Drain = PhaserFireSeconds > 0.f ? DeltaTime / PhaserFireSeconds : 1.f;
		PhaserEnergy = FMath::Max(0.f, PhaserEnergy - Drain);
	}
}

float UStarshipWeaponsComponent::GetTorpedoReadiness() const
{
	float Best = TorpedoTubeOffsets.Num() > 0 ? 0.f : 1.f;
	for (int32 i = 0; i < TorpedoTubeOffsets.Num(); ++i)
	{
		Best = FMath::Max(Best, GetTubeReadiness(i));
	}
	return Best;
}

float UStarshipWeaponsComponent::GetTubeReadiness(int32 Tube) const
{
	if (!TubeTimers.IsValidIndex(Tube) || TorpedoCooldown <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(1.f - TubeTimers[Tube] / TorpedoCooldown, 0.f, 1.f);
}

bool UStarshipWeaponsComponent::FireTorpedo()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || TorpedoTubeOffsets.Num() == 0)
	{
		return false;
	}
	if (TubeTimers.Num() != TorpedoTubeOffsets.Num())
	{
		TubeTimers.Init(0.f, TorpedoTubeOffsets.Num());
	}
	// Tubes alternate; a tube still reloading is skipped, so with two tubes a pair goes out back to back.
	int32 TubeIndex = INDEX_NONE;
	for (int32 k = 0; k < TorpedoTubeOffsets.Num(); ++k)
	{
		const int32 Candidate = (NextTube + k) % TorpedoTubeOffsets.Num();
		if (TubeTimers[Candidate] <= 0.f)
		{
			TubeIndex = Candidate;
			break;
		}
	}
	if (TubeIndex == INDEX_NONE)
	{
		return false;
	}
	NextTube = (TubeIndex + 1) % TorpedoTubeOffsets.Num();

	const FTransform ShipTransform = Owner->GetActorTransform();
	const FVector Tube = ShipTransform.TransformPosition(TorpedoTubeOffsets[TubeIndex]);
	// Straight out of the tube along the hull's axis; the torpedo itself banks toward the target once clear.
	const FVector ShipForward = ShipTransform.GetUnitAxis(EAxis::X);
	const FVector AimDir = (GetAimPoint() - Tube).GetSafeNormal();

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APhotonTorpedo* Torpedo = World->SpawnActor<APhotonTorpedo>(TorpedoClass ? *TorpedoClass : APhotonTorpedo::StaticClass(), Tube, ShipForward.Rotation(), Params);
	if (!Torpedo)
	{
		return false;
	}

	double ShipSpeed = 0.0;
	if (const UStarshipMovementComponent* Movement = Owner->FindComponentByClass<UStarshipMovementComponent>())
	{
		ShipSpeed = FVector::DotProduct(Movement->Velocity, ShipForward);
	}
	Torpedo->Launch(ShipForward, ShipSpeed, LockedTarget, AimDir);
	// The tube is inside our own hull volume: make sure our sweep never treats the torpedo as a wall.
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		Root->IgnoreActorWhenMoving(Torpedo, true);
	}
	TubeTimers[TubeIndex] = TorpedoCooldown;
	++TorpedoesFired;
	return true;
}

void UStarshipWeaponsComponent::CycleTarget()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector ShipLoc = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector();

	struct FCandidate { AActor* Actor; double Score; };
	TArray<FCandidate> Candidates;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A == Owner || !IsValid(A) || !A->ActorHasTag(ASpaceTarget::TargetableTag))
		{
			continue;
		}
		const FVector To = A->GetActorLocation() - ShipLoc;
		const double Dist = To.Size();
		if (Dist > TargetScanRange || Dist <= 0.0)
		{
			continue;
		}
		// Prefer things in front, then close.
		const double Cos = FVector::DotProduct(To / Dist, Forward);
		const double Score = (1.0 - Cos) * 1.0e6 + Dist / TargetScanRange;
		Candidates.Add({ A, Score });
	}
	if (Candidates.Num() == 0)
	{
		LockedTarget = nullptr;
		return;
	}
	Candidates.Sort([](const FCandidate& L, const FCandidate& R) { return L.Score < R.Score; });

	int32 CurrentIndex = INDEX_NONE;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (Candidates[i].Actor == LockedTarget)
		{
			CurrentIndex = i;
			break;
		}
	}
	LockedTarget = Candidates[(CurrentIndex + 1) % Candidates.Num()].Actor;
}

void UStarshipWeaponsComponent::ClearTarget()
{
	LockedTarget = nullptr;
}

void UStarshipWeaponsComponent::LockNearest()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const FVector ShipLoc = Owner->GetActorLocation();
	AActor* Best = nullptr;
	double BestDist = TargetScanRange;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A == Owner || !IsValid(A) || !A->ActorHasTag(ASpaceTarget::TargetableTag))
		{
			continue;
		}
		const double Dist = FVector::Dist(A->GetActorLocation(), ShipLoc);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = A;
		}
	}
	LockedTarget = Best;
}

void UStarshipWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LockedTarget && !IsValid(LockedTarget))
	{
		LockedTarget = nullptr;
	}
	if (TubeTimers.Num() != TorpedoTubeOffsets.Num())
	{
		TubeTimers.Init(0.f, TorpedoTubeOffsets.Num());
	}
	for (float& Timer : TubeTimers)
	{
		Timer = FMath::Max(0.f, Timer - DeltaTime);
	}
	UpdatePhasers(DeltaTime);
}
