#include "SlonkGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/WorldSettings.h"
#include "SpaceTarget.h"
#include "StarshipHUD.h"
#include "StarshipPawn.h"
#include "StarshipPlayerController.h"

ASlonkGameMode::ASlonkGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AStarshipPawn::StaticClass();
	PlayerControllerClass = AStarshipPlayerController::StaticClass();
	HUDClass = AStarshipHUD::StaticClass();
}

void ASlonkGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	DisableWorldBoundsChecks();
}

void ASlonkGameMode::BeginPlay()
{
	Super::BeginPlay();
	DisableWorldBoundsChecks();

	if (bSpawnTestTargets)
	{
		SpawnTestTargets();
	}
}

void ASlonkGameMode::DisableWorldBoundsChecks()
{
	// Space has no floor: never kill actors for falling below KillZ, and let the ship roam the LWC range.
	if (AWorldSettings* WorldSettings = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr)
	{
		WorldSettings->bEnableWorldBoundsChecks = false;
		WorldSettings->KillZ = -UE_OLD_HALF_WORLD_MAX;
	}
}

void ASlonkGameMode::SpawnTestTargets()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FRandomStream Rng(TestTargetSeed);
	for (int32 i = 0; i < NumTestTargets; ++i)
	{
		// Mostly ahead of the player start (+X) so targets are easy to find, but with a spread.
		FVector Dir = Rng.GetUnitVector();
		Dir.X = FMath::Abs(Dir.X) + 0.5;
		Dir.Normalize();
		const double Distance = Rng.FRandRange(TestTargetMinDistance, TestTargetMaxDistance);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASpaceTarget* Target = World->SpawnActor<ASpaceTarget>(ASpaceTarget::StaticClass(), Dir * Distance, Rng.GetUnitVector().Rotation(), Params);
		if (Target)
		{
			Target->RandomizeAppearance(Rng);
		}
	}
}

void ASlonkGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bTrackStarLighting)
	{
		UpdateStarLighting();
	}
}

void ASlonkGameMode::UpdateStarLighting()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const FVector Here = Pawn->GetActorLocation();

	AActor* Star = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(APlanetBody::StarTag))
		{
			const double D = FVector::DistSquared(It->GetActorLocation(), Here);
			if (D < BestDist)
			{
				BestDist = D;
				Star = *It;
			}
		}
	}
	if (!Star)
	{
		return;
	}
	const FVector LightDir = (Here - Star->GetActorLocation()).GetSafeNormal();
	if (LightDir.IsNearlyZero())
	{
		return;
	}
	const AStarBody* StarBody = Cast<AStarBody>(Star);
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		It->SetActorRotation(LightDir.Rotation());
		if (StarBody)
		{
			if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				// Warm/cool the sunlight with the star, keeping it bright.
				const FLinearColor C = StarBody->StarColor / FMath::Max(StarBody->StarColor.GetMax(), KINDA_SMALL_NUMBER);
				LightComp->SetLightColor(FLinearColor(FMath::Lerp(1.f, C.R, 0.6f), FMath::Lerp(1.f, C.G, 0.6f), FMath::Lerp(1.f, C.B, 0.6f)));
			}
		}
	}
}
