#include "SlonkGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "StarshipPawn.h"
#include "SlonkTiming.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "FloatingOriginComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/WorldSettings.h"
#include "SpaceTarget.h"
#include "StarshipHUD.h"
#include "StarshipPawn.h"
#include "StarshipPlayerController.h"

ASlonkGameMode::ASlonkGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	FloatingOrigin = CreateDefaultSubobject<UFloatingOriginComponent>(TEXT("FloatingOrigin"));
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

UFloatingOriginComponent* ASlonkGameMode::FindFloatingOrigin(const UWorld* World)
{
	if (const ASlonkGameMode* GM = World ? Cast<ASlonkGameMode>(World->GetAuthGameMode()) : nullptr)
	{
		return GM->FloatingOrigin;
	}
	return nullptr;
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
	FSlonkScopeTimer SlonkTimer_(TEXT("Tick SlonkGameMode"));
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
		if (!It->Style.bAtmosphere || It->RadiusKm > 10000.f)
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
			AtmospherePlanet->SetUsingVolumetricClouds(false);
		}
		AtmospherePlanet = Best;
	}

	// The real-time sky capture only has a sky to capture while an atmosphere is active; without one the
	// renderer warns on screen and the capture is black anyway.
	SetSkyCaptureActive(AtmospherePlanet != nullptr);
	if (!AtmospherePlanet)
	{
		if (SkyAtmosphere && SkyAtmosphere->IsVisible())
		{
			SkyAtmosphere->SetVisibility(false);
		}
		if (VolumetricCloud && VolumetricCloud->IsVisible())
		{
			VolumetricCloud->SetVisibility(false);
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
		// Volumetric clouds ride the same planet: with a Sky Atmosphere in the scene the renderer takes the
		// cloud layer's centre and ground radius from it, so the layer wraps this planet wherever it is.
		VolumetricCloud = NewObject<UVolumetricCloudComponent>(AtmosphereHolder, TEXT("VolumetricCloud"));
		VolumetricCloud->SetupAttachment(Root);
		if (UMaterialInterface* CloudMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
		{
			VolumetricCloud->SetMaterial(CloudMat);
		}
		VolumetricCloud->SetLayerBottomAltitude(CloudLayerBottomKm);
		VolumetricCloud->SetLayerHeight(CloudLayerHeightKm);
		VolumetricCloud->SetbUsePerSampleAtmosphericLightTransmittance(true);
		VolumetricCloud->SetViewSampleCountScale(0.6f);
		VolumetricCloud->SetShadowViewSampleCountScale(0.5f);
		VolumetricCloud->SetVisibility(false);
		VolumetricCloud->RegisterComponent();
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
			// Without this the engine applies one global transmittance, evaluated at ground level with the
			// atmosphere's +Z as "up". Our sun lies in the orbital plane, so that is a permanent sunset: every
			// lit surface came out dark maroon. Per pixel, each point gets its own sun elevation.
			if (!LightComp->bPerPixelAtmosphereTransmittance)
			{
				LightComp->bPerPixelAtmosphereTransmittance = true;
				LightComp->MarkRenderStateDirty();
			}
			// The clouds throw their shadows on the ground and take the ship's and the terrain's; the sun also
			// shades the atmosphere through them.
			if (!LightComp->bCastCloudShadows || !LightComp->bCastShadowsOnClouds)
			{
				LightComp->bCastCloudShadows = true;
				LightComp->bCastShadowsOnClouds = true;
				LightComp->bCastShadowsOnAtmosphere = true;
				LightComp->CloudShadowStrength = CloudShadowStrength;
				LightComp->CloudShadowExtent = 150.f;
				// The shadow map ray-marches the layer per texel: 512 square cost 10 ms on an M1 Pro, 256 costs 3,
				// and cloud shadows are soft anyway.
				LightComp->CloudShadowMapResolutionScale = 0.5f;
				LightComp->CloudShadowRaySampleCountScale = 0.5f;
				LightComp->MarkRenderStateDirty();
			}
		}
	}

	AtmosphereHolder->SetActorLocation(AtmospherePlanet->GetActorLocation());
	const FPlanetStyle& S = AtmospherePlanet->Style;
	const float RadiusKm = AtmospherePlanet->RadiusKm;
	// The style's AtmosphereDepth sizes the far glow shell; the real scattering layer is much thinner
	// (Earth: 1% of the radius), otherwise the limb from orbit becomes a thick white ring.
	// Per-world layer from the style; the old formula stays as the fallback for hand-placed planets.
	const float ScaleKm = S.AtmosphereScaleHeightKm > 0.f ? S.AtmosphereScaleHeightKm : FMath::Clamp(S.AtmosphereDepth * RadiusKm * 0.3f, 8.f, 120.f) * (8.f / 60.f);
	const float HeightKm = S.AtmosphereHeightKm > 0.f ? S.AtmosphereHeightKm : ScaleKm * 7.5f;
	// The atmosphere's ground sits a little below the rendered surface: pixels at or under the ground radius get
	// the full-atmosphere transmittance (black) from the aerial perspective LUT, which blacked out the un-displaced
	// far sphere and every ocean.
	// 0.05% of the radius (3 km on an Earth) keeps the sea off the LUT's ground while the air at the surface stays
	// near full density; the old 0.8% put the surface 45 km up a real atmosphere.
	SkyAtmosphere->SetBottomRadius(RadiusKm * 0.9995f);
	SkyAtmosphere->SetAtmosphereHeight(HeightKm);
	// Scale heights proportional to the atmosphere thickness (Earth: 60 km / 8 km / 1.2 km).
	SkyAtmosphere->SetRayleighExponentialDistribution(ScaleKm);
	SkyAtmosphere->SetMieExponentialDistribution(ScaleKm * 0.25f);
	// Artistic Rayleigh: the planet's atmosphere colour decides which wavelengths scatter most.
	// Raised to the 1.6 so the style's sky-blue (0.35, 0.6, 1) becomes the real Rayleigh ratio (0.18, 0.41, 1);
	// taken straight it scatters too much red and the sky goes grey.
	const FLinearColor Col0 = S.AtmosphereColor / FMath::Max(S.AtmosphereColor.GetMax(), KINDA_SMALL_NUMBER);
	const FLinearColor Col(FMath::Pow(Col0.R, 1.6f), FMath::Pow(Col0.G, 1.6f), FMath::Pow(Col0.B, 1.6f));
	SkyAtmosphere->SetRayleighScattering(FLinearColor(FMath::Max(Col.R, 0.08f), FMath::Max(Col.G, 0.08f), FMath::Max(Col.B, 0.08f)));
	// Thinner atmospheres need stronger coefficients to show at all; Earth is 0.0331 at 60 km.
	// Earth is 0.0331 / 0.004 at 60 km; keep that density regardless of planet size, scaled by the style's intensity.
	// A deeper layer gets a proportionally weaker coefficient (optical depth grows only as the 0.4 power of the
	// scale height), so thickening the atmosphere widens the limb instead of whiting it out.
	const float DepthScale = FMath::Pow(16.f / FMath::Max(ScaleKm, 1.f), 0.6f);
	SkyAtmosphere->SetRayleighScatteringScale(0.0331f * (S.Intensity / 2.2f) * 0.7f * DepthScale);
	SkyAtmosphere->SetMieScatteringScale(0.003996f * (0.5f + S.Haze * 8.f) * DepthScale);
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
	const float ApBoost = FMath::Lerp(FMath::Max(SkyLuminanceBoost * 0.5f, 1.f), 1.f, Blend);
	// The captured sky is full daylight wherever the sky light sits, so in orbit it floods everything with
	// ambient (no dark sides, no visible sun glint). Fade it out with altitude; a trace stays as planetshine.
	ApplySkyLightIntensity(SkyLightIntensityNearPlanet * FMath::Lerp(1.f, 0.04f, Blend));
	// Daylight: inside the atmosphere with the sun up, the sky outshines the star field, so hide it (the
	// sprites are sized for space and would burn through the blue).
	if (APawn* ViewPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (AStarshipPawn* Ship = Cast<AStarshipPawn>(ViewPawn))
		{
			const FVector Up = (ViewLoc - AtmospherePlanet->GetActorLocation()).GetSafeNormal();
			const float SunUp = (float)FVector::DotProduct(Up, AtmospherePlanet->GetStarDirection());
			const float Daylight = (1.f - FMath::Clamp((float)(AltitudeKm / (HeightKm * 1.2)), 0.f, 1.f)) * FMath::Clamp(SunUp * 6.f + 0.3f, 0.f, 1.f);
			Ship->SetStarFieldHidden(Daylight > 0.5f);
		}
	}
	SkyAtmosphere->SetSkyLuminanceFactor(FLinearColor(Boost, Boost, Boost));
	SkyAtmosphere->SetSkyAndAerialPerspectiveLuminanceFactor(FLinearColor(ApBoost, ApBoost, ApBoost));
	SkyAtmosphere->SetMultiScatteringFactor(1.f);
	if (!SkyAtmosphere->IsVisible())
	{
		SkyAtmosphere->SetVisibility(true);
	}
	AtmospherePlanet->SetUsingSkyAtmosphere(true);
	// Volumetric clouds near the planet, the shell further out (the shell reads from orbit; the ray-marched
	// layer is for the sky overhead and the shadows it throws).
	if (VolumetricCloud)
	{
		const bool bVolumetric = bUseVolumetricClouds && S.bClouds && BestRadii < VolumetricCloudRadii;
		if (VolumetricCloud->IsVisible() != bVolumetric)
		{
			VolumetricCloud->SetVisibility(bVolumetric);
		}
		AtmospherePlanet->SetUsingVolumetricClouds(bVolumetric);
	}
}

void ASlonkGameMode::ApplySkyLightIntensity(float Intensity)
{
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		if (USkyLightComponent* Sky = It->GetLightComponent())
		{
			if (!FMath::IsNearlyEqual(Sky->Intensity, Intensity, 1e-3f))
			{
				Sky->SetIntensity(Intensity);
			}
		}
	}
}

void ASlonkGameMode::SetSkyCaptureActive(bool bActive)
{
	if (bSkyCaptureActive == bActive && bSkyCaptureInitialised)
	{
		return;
	}
	bSkyCaptureActive = bActive;
	bSkyCaptureInitialised = true;
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		if (USkyLightComponent* Sky = It->GetLightComponent())
		{
			Sky->SetRealTimeCaptureEnabled(bActive);
			Sky->SetIntensity(bActive ? SkyLightIntensityNearPlanet : 0.f);
		}
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

	StarSearchTimer -= World->GetDeltaSeconds();
	if (!CachedStar.IsValid() || StarSearchTimer <= 0.f)
	{
		StarSearchTimer = 1.f;
		AActor* Best = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(APlanetBody::StarTag))
			{
				const double D = FVector::DistSquared(It->GetActorLocation(), Here);
				if (D < BestDist)
				{
					BestDist = D;
					Best = *It;
				}
			}
		}
		CachedStar = Best;
	}
	AActor* Star = CachedStar.Get();
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
		// Rotating a directional light invalidates every cached / virtual shadow page, so only move it when
		// the direction has drifted by a visible amount (about 0.1 degrees).
		const FVector CurrentDir = It->GetActorForwardVector();
		if (FVector::DotProduct(CurrentDir, LightDir) < FMath::Cos(FMath::DegreesToRadians(0.1f)))
		{
			It->SetActorRotation(LightDir.Rotation());
		}
		if (StarBody)
		{
			if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				// Warm/cool the sunlight with the star, keeping it bright.
				const FLinearColor C = StarBody->StarColor / FMath::Max(StarBody->StarColor.GetMax(), KINDA_SMALL_NUMBER);
				const FLinearColor Wanted(FMath::Lerp(1.f, C.R, 0.6f), FMath::Lerp(1.f, C.G, 0.6f), FMath::Lerp(1.f, C.B, 0.6f));
				if (!LightComp->GetLightColor().Equals(Wanted, 0.002f))
				{
					LightComp->SetLightColor(Wanted);
				}
			}
		}
	}
}
