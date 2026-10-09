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

	// Sensible defaults for a 305 m Constitution-refit hull centred on the root: tweak in the pawn.
	PhaserEmitterOffsets = { FVector(9000.0, -2500.0, -900.0), FVector(9000.0, 2500.0, -900.0) };
	TorpedoTubeOffsets = { FVector(3000.0, -250.0, 700.0), FVector(3000.0, 250.0, 700.0) };
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
	const FVector ShipForward = ShipTransform.GetUnitAxis(EAxis::X);

	// Spawn beams lazily, one per emitter.
	while (ActiveBeams.Num() < PhaserEmitterOffsets.Num())
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

	bool bAnyFired = false;
	for (int32 i = 0; i < ActiveBeams.Num() && i < PhaserEmitterOffsets.Num(); ++i)
	{
		APhaserBeam* Beam = ActiveBeams[i];
		if (!Beam)
		{
			continue;
		}
		const FVector Start = ShipTransform.TransformPosition(PhaserEmitterOffsets[i]);
		FVector Dir = (AimPoint - Start).GetSafeNormal();
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, ShipForward), -1.0, 1.0)));
		if (AngleDeg > PhaserFiringArc)
		{
			Beam->SetActorHiddenInGame(true);
			continue;
		}

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PhaserBeam), false);
		Params.AddIgnoredActor(Owner);
		const FVector TraceEnd = Start + Dir * PhaserRange;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Start, TraceEnd, ECC_Visibility, Params);
		const FVector End = bHit ? Hit.ImpactPoint : TraceEnd;
		Beam->SetBeam(Start, End, bHit);
		bAnyFired = true;

		if (bHit && Hit.GetActor())
		{
			AController* Instigator = Cast<APawn>(Owner) ? Cast<APawn>(Owner)->GetController() : nullptr;
			UGameplayStatics::ApplyPointDamage(Hit.GetActor(), PhaserDamagePerSecond * DeltaTime, Dir, Hit, Instigator, Owner, nullptr);
		}
	}

	if (bAnyFired)
	{
		const float Drain = PhaserFireSeconds > 0.f ? DeltaTime / PhaserFireSeconds : 1.f;
		PhaserEnergy = FMath::Max(0.f, PhaserEnergy - Drain);
	}
}

bool UStarshipWeaponsComponent::FireTorpedo()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || TorpedoTimer > 0.f || TorpedoTubeOffsets.Num() == 0)
	{
		return false;
	}

	const FTransform ShipTransform = Owner->GetActorTransform();
	const FVector Tube = ShipTransform.TransformPosition(TorpedoTubeOffsets[NextTube % TorpedoTubeOffsets.Num()]);
	NextTube = (NextTube + 1) % TorpedoTubeOffsets.Num();

	const FVector AimPoint = GetAimPoint();
	FVector Dir = (AimPoint - Tube).GetSafeNormal();
	const FVector ShipForward = ShipTransform.GetUnitAxis(EAxis::X);
	// Torpedoes leave the tube roughly forward and then home; don't let them fire backwards.
	if (FVector::DotProduct(Dir, ShipForward) < 0.3)
	{
		Dir = ShipForward;
	}

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APhotonTorpedo* Torpedo = World->SpawnActor<APhotonTorpedo>(TorpedoClass ? *TorpedoClass : APhotonTorpedo::StaticClass(), Tube, Dir.Rotation(), Params);
	if (!Torpedo)
	{
		return false;
	}

	double ShipSpeed = 0.0;
	if (const UStarshipMovementComponent* Movement = Owner->FindComponentByClass<UStarshipMovementComponent>())
	{
		ShipSpeed = FVector::DotProduct(Movement->Velocity, Dir);
	}
	Torpedo->Launch(Dir, ShipSpeed, LockedTarget);
	// The tube is inside our own hull volume: make sure our sweep never treats the torpedo as a wall.
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		Root->IgnoreActorWhenMoving(Torpedo, true);
	}
	TorpedoTimer = TorpedoCooldown;
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

void UStarshipWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LockedTarget && !IsValid(LockedTarget))
	{
		LockedTarget = nullptr;
	}
	TorpedoTimer = FMath::Max(0.f, TorpedoTimer - DeltaTime);
	UpdatePhasers(DeltaTime);
}
