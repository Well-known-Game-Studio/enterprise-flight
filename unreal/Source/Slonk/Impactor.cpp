// Slonk - the impactor: a rock on a course, pushed by the beam, broken by the guns, or landing.

#include "Impactor.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "StarSystem.h"
#include "StarshipPawn.h"
#include "TractorBeamComponent.h"
#include "SlonkGameMode.h"
#include "FloatingOriginComponent.h"
#include "GameClock.h"
#include "Missions.h"
#include "ExplosionEffect.h"
#include "SlonkMaterials.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AImpactor::AImpactor()
{
	Coma = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Coma"));
	Coma->SetupAttachment(Body);
	Coma->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Coma->SetVisibility(false);
	Coma->SetCastShadow(false);
	Tail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tail"));
	Tail->SetupAttachment(Body);
	Tail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tail->SetVisibility(false);
	Tail->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (SphereFinder.Succeeded()) { Coma->SetStaticMesh(SphereFinder.Object); }
	if (ConeFinder.Succeeded()) { Tail->SetStaticMesh(ConeFinder.Object); }
}

// ---- the track (pure) -----------------------------------------------------------------------

double AImpactor::TrackMissDistance(const FVector& Pos, const FVector& Vel, const FVector& Point)
{
	const double V2 = Vel.SizeSquared();
	if (V2 <= 0.0) { return FVector::Dist(Pos, Point); }
	const double T = FMath::Max(0.0, FVector::DotProduct(Point - Pos, Vel) / V2);
	return FVector::Dist(Pos + Vel * T, Point);
}

double AImpactor::TrackSecondsToClosest(const FVector& Pos, const FVector& Vel, const FVector& Point)
{
	const double V2 = Vel.SizeSquared();
	if (V2 <= 0.0) { return 0.0; }
	return FMath::Max(0.0, FVector::DotProduct(Point - Pos, Vel) / V2);
}

// ---- spawning ---------------------------------------------------------------------------------

AImpactor* AImpactor::Spawn(UWorld* W, APlanetBody* Target, const FString& Name, double HoursToImpact, bool bComet, int32 Seed)
{
	if (!W || !Target) { return nullptr; }
	FRandomStream R(Seed);
	// A course from a bearing off the plane, a little aslant so the ship sees it cross: in at twenty-five
	// kilometres a second from as far out as the hours say.
	FVector Dir = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-0.35f, 0.35f)).GetSafeNormal();
	if (Dir.IsNearlyZero()) { Dir = FVector(1, 0, 0); }
	const double Dist = SpeedCmPerS * HoursToImpact * 3600.0 + Target->GetRadiusCm();
	const FVector Loc = Target->GetActorLocation() - Dir * Dist;
	AImpactor* A = W->SpawnActorDeferred<AImpactor>(AImpactor::StaticClass(), FTransform(Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!A) { return nullptr; }
	A->DisplayName = Name; A->WorldName = Target->GetDisplayName(); A->TargetWorld = Target; A->bComet = bComet;
	A->Radius = R.FRandRange(60000.f, 160000.f);   // six hundred metres to a kilometre and a half of rock
	A->MaxHealth = 150.f + A->Radius / 400.f; A->Health = A->MaxHealth;
	A->TumbleRate = FRotator(R.FRandRange(-4.f, 4.f), R.FRandRange(-4.f, 4.f), R.FRandRange(-4.f, 4.f));
	A->Velocity = Dir * SpeedCmPerS;
	A->FinishSpawning(FTransform(Loc));
	A->Universe = A->ToUniverse(Loc); A->bPlaced = true;
	UE_LOG(LogTemp, Log, TEXT("Impactor %s: %s, %.0f km across, %.1f h from %s at %.0f million km"), *Name, bComet ? TEXT("a comet") : TEXT("an asteroid"), A->Radius * 2.0 / 1.0e5, HoursToImpact, *A->WorldName, Dist / 1.0e11);
	return A;
}

AImpactor* AImpactor::FindByName(UWorld* World, const FString& Name)
{
	if (!World) { return nullptr; }
	for (TActorIterator<AImpactor> It(World); It; ++It) { if (It->DisplayName == Name) { return *It; } }
	return nullptr;
}

void AImpactor::BeginPlay()
{
	Super::BeginPlay();
	if (!bPlaced) { Universe = ToUniverse(GetActorLocation()); bPlaced = true; }
	if (bComet) { BuildComet(); }
}

void AImpactor::BuildComet()
{
	// A coma three radii across and a tail forty long, in the glow material: dirty ice seen from close by.
	UMaterialInterface* Glow = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("CometGlow"));
	for (UStaticMeshComponent* C : { Coma.Get(), Tail.Get() })
	{
		if (!C) { continue; }
		if (Glow)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glow, this);
			if (M) { M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.75f, 1.f) * (C == Coma ? 1.5f : 0.6f)); C->SetMaterial(0, M); }
		}
		C->SetVisibility(true);
	}
	// The body's scale is the radius; the parts are scaled against it (the engine sphere and cone are 100 cm).
	Coma->SetRelativeScale3D(FVector(3.0));
	Tail->SetRelativeScale3D(FVector(2.2, 2.2, 40.0));
	UpdateTail();
}

// ---- motion -----------------------------------------------------------------------------------

FVector AImpactor::ToUniverse(const FVector& W) const
{
	if (const UFloatingOriginComponent* O = ASlonkGameMode::FindFloatingOrigin(GetWorld())) { return O->WorldToUniverse(W); }
	return W;
}

FVector AImpactor::ToWorld(const FVector& U) const
{
	if (const UFloatingOriginComponent* O = ASlonkGameMode::FindFloatingOrigin(GetWorld())) { return O->UniverseToWorld(U); }
	return U;
}

FVector AImpactor::WorldUniverse() const
{
	return TargetWorld.IsValid() ? ToUniverse(TargetWorld->GetActorLocation()) : Universe + Velocity;
}

double AImpactor::MissDistanceCm() const { return TrackMissDistance(Universe, Velocity, WorldUniverse()); }
double AImpactor::HoursToClosest() const { return TrackSecondsToClosest(Universe, Velocity, WorldUniverse()) / 3600.0; }
FString AImpactor::GetWorldName() const { return TargetWorld.IsValid() ? TargetWorld->GetDisplayName() : WorldName; }
bool AImpactor::WillHit() const { return TargetWorld.IsValid() && MissDistanceCm() < TargetWorld->GetRadiusCm() + Radius; }

void AImpactor::Push(const FVector& DeltaV)
{
	Velocity += DeltaV;
}

void AImpactor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float SimDt = UGameClockSubsystem::Sim(this, DeltaTime);
	// Under the beam: the beam has moved the rock (its hold on the clock makes these real seconds) and the push
	// it gives is a change of course toward the ship, the longer the harder.
	bool bHeld = false;
	for (TActorIterator<AStarshipPawn> It(GetWorld()); It; ++It)
	{
		if (It->TractorBeam && It->TractorBeam->IsBeamActive() && It->TractorBeam->Target == this)
		{
			bHeld = true;
			Universe = ToUniverse(GetActorLocation());
			// The push that counts is across the track: the helm holds the ship off the rock's line, and the beam's
			// pull bends the course toward her rather than hurrying or slowing the rock along it.
			const FVector ToShip = (It->GetActorLocation() - GetActorLocation()).GetSafeNormal();
			const FVector Along = Velocity.GetSafeNormal();
			FVector Side = ToShip - Along * FVector::DotProduct(ToShip, Along);
			if (Side.SizeSquared() < 0.01) { Side = FVector::CrossProduct(Along, FVector(0, 0, 1)); if (Side.IsNearlyZero()) { Side = FVector::CrossProduct(Along, FVector(0, 1, 0)); } }
			Velocity += Side.GetSafeNormal() * TractorPushCmPerS2 * SimDt;
			break;
		}
	}
	Universe += Velocity * SimDt;
	if (!bHeld) { SetActorLocation(ToWorld(Universe), false, nullptr, ETeleportType::TeleportPhysics); }
	if (bComet) { TailTimer -= DeltaTime; if (TailTimer <= 0.f) { TailTimer = 0.5f; UpdateTail(); } }
	if (TargetWorld.IsValid())
	{
		const double D = FVector::Dist(Universe, WorldUniverse());
		if (D <= TargetWorld->GetRadiusCm() + Radius) { Strike(); return; }
		const double R = TargetWorld->GetRadiusCm();
		const bool bPast = TrackSecondsToClosest(Universe, Velocity, WorldUniverse()) <= 0.0;
		// Averted (off its course by a margin, nothing bending it back; or past the world): the mission is told once,
		// and the rock sails on, a rock like any other, until it is well past the world and retired.
		if (!bResolved && ((!bHeld && MissDistanceCm() > R * 1.5 + Radius) || (bPast && D > R * 3.0))) { Resolve(EImpactOutcome::Averted); }
		if (bResolved && bPast && D > RetireCm && !bHeld) { Destroy(); return; }
	}
}

void AImpactor::UpdateTail()
{
	if (!Tail) { return; }
	// Away from the nearest star: the cone's point is its +Z, so the base sits on the rock and the point streams out.
	FVector Away = FVector(0, 0, 1);
	double Best = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (D < Best) { Best = D; Away = (GetActorLocation() - It->GetActorLocation()).GetSafeNormal(); }
	}
	const FQuat Q = FQuat::FindBetweenNormals(FVector(0, 0, 1), Away);
	Tail->SetWorldRotation(Q);
	Tail->SetWorldLocation(GetActorLocation() + Away * Radius * 20.0);
	Tail->SetWorldScale3D(FVector(Radius * 2.2 / 50.0, Radius * 2.2 / 50.0, Radius * 40.0 / 50.0));
	if (Coma) { Coma->SetWorldScale3D(FVector(Radius * 3.0 / 50.0)); }
}

// ---- ends -------------------------------------------------------------------------------------

float AImpactor::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (Health <= 0.f) { return 0.f; }
	const float Applied = AActor::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health -= Applied;
	if (Health <= 0.f) { Shatter(); }
	return Applied;
}

void AImpactor::Shatter()
{
	if (Health <= 0.f && bResolved) { return; }   // already in pieces
	Health = 0.f;
	AExplosionEffect::SpawnExplosion(GetWorld(), GetActorLocation(), Radius * 3.f, 1.6f);
	// The pieces: three rocks a third the size, scattered, going nowhere.
	FRandomStream R(GetTypeHash(DisplayName));
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector Loc = GetActorLocation() + R.GetUnitVector() * Radius * R.FRandRange(2.f, 5.f);
		ASpaceTarget* F = GetWorld()->SpawnActorDeferred<ASpaceTarget>(ASpaceTarget::StaticClass(), FTransform(Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!F) { continue; }
		F->Radius = Radius * R.FRandRange(0.25f, 0.4f); F->MaxHealth = 40.f; F->Health = 40.f;
		F->TumbleRate = FRotator(R.FRandRange(-8.f, 8.f), R.FRandRange(-8.f, 8.f), R.FRandRange(-8.f, 8.f));
		F->FinishSpawning(FTransform(Loc));
	}
	Resolve(EImpactOutcome::Shattered);
	Destroy();
}

void AImpactor::Strike()
{
	if (bStruck) { return; }
	AExplosionEffect::SpawnExplosion(GetWorld(), GetActorLocation(), Radius * 12.f, 3.0f);
	Resolve(EImpactOutcome::Struck);
	Destroy();
}

void AImpactor::Resolve(EImpactOutcome Outcome)
{
	// Averted or shattered is said once. A landing is said even after an averted call (a rock bent back onto
	// the world lands all the same), once.
	if (Outcome == EImpactOutcome::Struck) { if (bStruck) { return; } bStruck = true; }
	else if (bResolved) { return; }
	bResolved = true;
	if (UMissionSubsystem* M = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr) { M->ImpactOutcome(DisplayName, GetWorldName(), Outcome); }
}

void AImpactor::DevDeflect()
{
	if (!TargetWorld.IsValid()) { return; }
	// A sideways change of course that clears the world by three radii from where it is now.
	const FVector ToW = WorldUniverse() - Universe;
	const FVector Side = FVector::CrossProduct(ToW.GetSafeNormal(), FVector(0, 0, 1)).GetSafeNormal();
	const double T = FMath::Max(TrackSecondsToClosest(Universe, Velocity, WorldUniverse()), 1.0);
	Velocity += Side * (TargetWorld->GetRadiusCm() * 3.0 / T);
}

void AImpactor::DevStrike()
{
	if (!TargetWorld.IsValid()) { return; }
	Universe = WorldUniverse() - Velocity.GetSafeNormal() * (TargetWorld->GetRadiusCm() + Radius * 0.5);
	SetActorLocation(ToWorld(Universe), false, nullptr, ETeleportType::TeleportPhysics);
}
