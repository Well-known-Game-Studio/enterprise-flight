#include "SlonkGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
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
	if (bUseSkyAtmosphere)
	{
		UpdateSkyAtmosphere();
	}
}

void ASlonkGameMode::UpdateSkyAtmosphere()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	// Nearest planet (in radii) that has an atmosphere.
	APlanetBody* Best = nullptr;
	double BestRadii = TNumericLimits<double>::Max();
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		if (!It->Style.bAtmosphere)
		{
			continue;
		}
		const double Radii = FVector::Dist(It->GetActorLocation(), ViewLoc) / It->GetRadiusCm();
		if (Radii < BestRadii)
		{
			BestRadii = Radii;
			Best = *It;
		}
	}
	if (Best && BestRadii > SkyAtmosphereRadii)
	{
		Best = nullptr;
	}

	if (Best != AtmospherePlanet)
	{
		if (AtmospherePlanet)
		{
			AtmospherePlanet->SetUsingSkyAtmosphere(false);
		}
		AtmospherePlanet = Best;
	}

	if (!AtmospherePlanet)
	{
		if (SkyAtmosphere && SkyAtmosphere->IsVisible())
		{
			SkyAtmosphere->SetVisibility(false);
		}
		return;
	}

	if (!AtmosphereHolder)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AtmosphereHolder = World->SpawnActor<AActor>(AActor::StaticClass(), AtmospherePlanet->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!AtmosphereHolder)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(AtmosphereHolder, TEXT("Root"));
		Root->RegisterComponent();
		AtmosphereHolder->SetRootComponent(Root);
		SkyAtmosphere = NewObject<USkyAtmosphereComponent>(AtmosphereHolder, TEXT("SkyAtmosphere"));
		SkyAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		SkyAtmosphere->SetupAttachment(Root);
		SkyAtmosphere->RegisterComponent();
	}

	// Sunlight must be flagged as an atmosphere light for the scattering to pick it up.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			if (!LightComp->bAtmosphereSunLight)
			{
				LightComp->SetAtmosphereSunLight(true);
			}
		}
	}

	AtmosphereHolder->SetActorLocation(AtmospherePlanet->GetActorLocation());
	const FPlanetStyle& S = AtmospherePlanet->Style;
	const float RadiusKm = AtmospherePlanet->RadiusKm;
	// The style's AtmosphereDepth sizes the far glow shell; the real scattering layer is much thinner
	// (Earth: 1% of the radius), otherwise the limb from orbit becomes a thick white ring.
	const float HeightKm = FMath::Clamp(S.AtmosphereDepth * RadiusKm * 0.25f, 6.f, 80.f);
	SkyAtmosphere->SetBottomRadius(RadiusKm);
	SkyAtmosphere->SetAtmosphereHeight(HeightKm);
	// Scale heights proportional to the atmosphere thickness (Earth: 60 km / 8 km / 1.2 km).
	SkyAtmosphere->SetRayleighExponentialDistribution(HeightKm * (8.f / 60.f));
	SkyAtmosphere->SetMieExponentialDistribution(HeightKm * (1.2f / 60.f));
	// Artistic Rayleigh: the planet's atmosphere colour decides which wavelengths scatter most.
	const FLinearColor Col = S.AtmosphereColor / FMath::Max(S.AtmosphereColor.GetMax(), KINDA_SMALL_NUMBER);
	SkyAtmosphere->SetRayleighScattering(FLinearColor(FMath::Max(Col.R, 0.1f), FMath::Max(Col.G, 0.1f), FMath::Max(Col.B, 0.1f)));
	// Thinner atmospheres need stronger coefficients to show at all; Earth is 0.0331 at 60 km.
	// Earth is 0.0331 / 0.004 at 60 km; keep that density regardless of planet size, scaled by the style's intensity.
	SkyAtmosphere->SetRayleighScatteringScale(0.0331f * (S.Intensity / 2.2f) * 0.7f);
	SkyAtmosphere->SetMieScatteringScale(0.003996f * (0.5f + S.Haze * 8.f));
	SkyAtmosphere->SetMieAnisotropy(0.8f);
	// Keep distant terrain readable from orbit.
	SkyAtmosphere->SetAerialPespectiveViewDistanceScale(0.35f);
	SkyAtmosphere->SetGroundAlbedo(S.OceanColor.ToFColor(true));
	// Full boost inside the atmosphere (the sky needs it to read), easing off with altitude so the limb seen
	// from orbit stays a thin blue line instead of a white ring.
	const double AltitudeKm = (FVector::Dist(ViewLoc, AtmospherePlanet->GetActorLocation()) - AtmospherePlanet->GetRadiusCm()) / 100000.0;
	const float Blend = FMath::Clamp((float)(AltitudeKm / (HeightKm * 8.0)), 0.f, 1.f);
	const float Boost = FMath::Lerp(SkyLuminanceBoost, FMath::Max(SkyLuminanceBoost * 0.3f, 1.f), Blend);
	// Aerial perspective washes the surface out from orbit at anything above 1x; only boost it when inside.
	const float ApBoost = FMath::Lerp(SkyLuminanceBoost, 1.f, Blend);
	SkyAtmosphere->SetSkyLuminanceFactor(FLinearColor(Boost, Boost, Boost));
	SkyAtmosphere->SetSkyAndAerialPerspectiveLuminanceFactor(FLinearColor(ApBoost, ApBoost, ApBoost));
	SkyAtmosphere->SetMultiScatteringFactor(1.f);
	if (!SkyAtmosphere->IsVisible())
	{
		SkyAtmosphere->SetVisibility(true);
	}
	AtmospherePlanet->SetUsingSkyAtmosphere(true);
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
