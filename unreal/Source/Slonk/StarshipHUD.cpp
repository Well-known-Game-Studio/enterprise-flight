#include "StarshipHUD.h"
#include "SlonkLoadSubsystem.h"

#include "Engine/Canvas.h"
#include "RHI.h"
#include "EngineUtils.h"
#include "PlanetBody.h"
#include "SpaceTarget.h"
#include "BeltField.h"
#include "SpaceVessel.h"
#include "BorgCube.h"
#include "TractorBeamComponent.h"
#include "StarBody.h"
#include "Nebula.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "ShuttlecraftPawn.h"
#include "StarshipWeaponsComponent.h"
#include "WarpDriveComponent.h"
#include "ScienceScannerComponent.h"
#include "ScienceLog.h"

AStarshipHUD::AStarshipHUD()
{
}

FString AStarshipHUD::FormatSpeed(double CmPerSecond) const
{
	const double KmPerSecond = CmPerSecond / 100000.0;
	if (KmPerSecond >= 1000.0)
	{
		// Fraction of c for the warp readout. 1c = 299,792 km/s.
		return FString::Printf(TEXT("%.0f km/s  (%.2f c)"), KmPerSecond, KmPerSecond / 299792.458);
	}
	if (KmPerSecond >= 1.0)
	{
		return FString::Printf(TEXT("%.2f km/s"), KmPerSecond);
	}
	return FString::Printf(TEXT("%.0f m/s"), CmPerSecond / 100.0);
}

FString AStarshipHUD::FormatDistance(double Cm) const
{
	const double Km = Cm / 100000.0;
	if (Km >= 9.0e10)
	{
		return FString::Printf(TEXT("%.2f ly"), Km / 9.4607e12);
	}
	if (Km >= 2.0e6)
	{
		return FString::Printf(TEXT("%.3f AU"), Km / 149597870.7);
	}
	if (Km >= 1000.0)
	{
		return FString::Printf(TEXT("%.0f km"), Km);
	}
	if (Km >= 1.0)
	{
		return FString::Printf(TEXT("%.1f km"), Km);
	}
	return FString::Printf(TEXT("%.0f m"), Cm / 100.0);
}

void AStarshipHUD::DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color) const
{
	if (!Canvas)
	{
		return;
	}
	Canvas->K2_DrawBox(FVector2D(X, Y), FVector2D(W, H), 1.f, FLinearColor(1, 1, 1, 0.35f));
	FCanvasTileItem Fill(FVector2D(X + 1, Y + 1), FVector2D(FMath::Max(0.f, (W - 2) * FMath::Clamp(Fraction, 0.f, 1.f)), H - 2), Color);
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Fill);
}

void AStarshipHUD::DrawReticle(float Scale)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float R = 14.f * Scale;
	const float Gap = 5.f * Scale;
	const FLinearColor C(Accent.R, Accent.G, Accent.B, 0.8f);
	DrawLine(CX - R, CY, CX - Gap, CY, C, 1.5f);
	DrawLine(CX + Gap, CY, CX + R, CY, C, 1.5f);
	DrawLine(CX, CY - R, CX, CY - Gap, C, 1.5f);
	DrawLine(CX, CY + Gap, CX, CY + R, C, 1.5f);
}

void AStarshipHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	const AStarshipPawn* Ship = Cast<AStarshipPawn>(PlayerOwner->GetPawn());
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float UIScale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.7f, 2.f);
	const float Line = 18.f * UIScale;
	const FLinearColor Dim(1, 1, 1, 0.75f);

	if (const USlonkLoadSubsystem* Load = USlonkLoadSubsystem::Get(this))
	{
		if (!Load->IsInitialLoadDone())
		{
			DrawLoadingScreen(*Load, Font, SmallFont, UIScale);
			return;
		}
	}

	if (!Ship)
	{
		DrawText(TEXT("No starship possessed"), Warning, 40.f, 40.f, Font, UIScale);
		return;
	}

	const UStarshipMovementComponent* Movement = Ship->Movement;
	const UWarpDriveComponent* Warp = Ship->WarpDrive;
	const UStarshipWeaponsComponent* Weapons = Ship->Weapons;

	DrawReticle(UIScale);

	// ---- Left panel: flight ----
	float X = 32.f * UIScale;
	float Y = 32.f * UIScale;
	DrawText(Ship->IsShuttle() ? Ship->GetCraftName().ToUpper() : FString(TEXT("U.S.S. ENTERPRISE  NCC-1701-A")), Accent, X, Y, Font, UIScale); Y += Line * 1.4f;

	// Frame timing (smoothed) so performance is visible without console stats.
	{
		const float Dt = GetWorld()->GetDeltaSeconds();
		SmoothedFrameMs = SmoothedFrameMs <= 0.f ? Dt * 1000.f : FMath::Lerp(SmoothedFrameMs, Dt * 1000.f, 0.05f);
		const float GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		SmoothedGpuMs = SmoothedGpuMs <= 0.f ? GpuMs : FMath::Lerp(SmoothedGpuMs, GpuMs, 0.05f);
		DrawText(FString::Printf(TEXT("%.0f FPS   frame %.1f ms   gpu %.1f ms"), SmoothedFrameMs > 0.f ? 1000.f / SmoothedFrameMs : 0.f, SmoothedFrameMs, SmoothedGpuMs), FLinearColor(1, 1, 1, 0.5f), X, Y, SmallFont, UIScale); Y += Line;
	}

	if (Movement)
	{
		const double Speed = Movement->GetSpeed();
		DrawText(FString::Printf(TEXT("SPEED    %s"), *FormatSpeed(Speed)), Dim, X, Y, Font, UIScale); Y += Line;

		const double Throttle = Movement->Throttle;
		FString ThrottleLabel;
		if (Warp && Warp->IsWarpEngaged())
		{
			ThrottleLabel = Warp->WarpFactor > Warp->TranswarpStart + 0.001f ? TEXT("TRANSWARP") : TEXT("WARP");
		}
		else if (FMath::IsNearlyZero(Throttle, 0.01))
		{
			ThrottleLabel = TEXT("ALL STOP");
		}
		else if (Throttle < 0.0)
		{
			ThrottleLabel = TEXT("REVERSE");
		}
		else if (Throttle >= 0.99)
		{
			ThrottleLabel = TEXT("FULL IMPULSE");
		}
		else
		{
			ThrottleLabel = FString::Printf(TEXT("IMPULSE %d%%"), FMath::RoundToInt(Throttle * 100.0));
		}
		DrawText(FString::Printf(TEXT("THROTTLE %s"), *ThrottleLabel), Dim, X, Y, Font, UIScale); Y += Line;
		DrawBar(X, Y + 3.f * UIScale, 180.f * UIScale, 8.f * UIScale, FMath::Abs(Throttle), Throttle < 0 ? Warning : Accent); Y += Line;
		if (!FMath::IsNearlyZero(Movement->LastVerticalInput))
		{
			DrawText(FString::Printf(TEXT("VERTICAL %s"), Movement->LastVerticalInput > 0.f ? TEXT("UP") : TEXT("DOWN")), Accent, X, Y, Font, UIScale); Y += Line;
		}
		if (Movement->bOrbitHold)
		{
			const double Alt = Movement->OrbitRadius - Movement->OrbitBodyRadius;
			const double Period = Movement->GetOrbitPeriod();
			const FString PeriodText = Period <= 0.0 ? TEXT("holding position") : (Period < 120.0 ? FString::Printf(TEXT("period %.0f s"), Period) : FString::Printf(TEXT("period %.1f min"), Period / 60.0));
			if (Movement->bFollowingVessel)
			{
				const double TargetSpeed = Movement->OrbitBody ? Movement->OrbitBody->GetVelocity().Size() : 0.0;
				DrawText(FString::Printf(TEXT("FOLLOWING  %s   range %s   target %s/s   (O releases, throttle circles, PgUp/PgDn range)"), *Movement->OrbitBodyName.ToUpper(), *FormatDistance(Movement->OrbitRadius), *FormatDistance(TargetSpeed)), Accent, X, Y, Font, UIScale); Y += Line;
			}
			else
			{
				DrawText(FString::Printf(TEXT("ORBIT HOLD  %s   alt %s   %s   (O releases, PgUp/PgDn alt)"), *Movement->OrbitBodyName.ToUpper(), *FormatDistance(Alt), *PeriodText), Accent, X, Y, Font, UIScale); Y += Line;
			}
		}
		if (const AShuttlecraftPawn* Shuttle = Cast<AShuttlecraftPawn>(Ship))
		{
			const AStarshipPawn* Home = Shuttle->Mothership.Get();
			const double BayDist = Shuttle->DistanceToBay();
			FString Status;
			switch (Shuttle->GetPhase())
			{
			case EShuttlePhase::Launching: Status = TEXT("leaving the bay"); break;
			case EShuttlePhase::Docking: Status = TEXT("docking"); break;
			default: Status = BayDist >= 0.0 && BayDist <= Shuttle->DockRangeCm ? TEXT("bay in range, C docks") : TEXT("C docks within 500 m of the bay"); break;
			}
			DrawText(FString::Printf(TEXT("SHUTTLECRAFT   %s %s   %s"), Home ? *Home->GetCraftName().ToUpper() : TEXT("NO SHIP"), BayDist >= 0.0 ? *FormatDistance(BayDist) : TEXT(""), *Status), Accent, X, Y, Font, UIScale); Y += Line;
		}
		if (Movement->GroundBody && Movement->AltitudeAGLCm >= 0.0 && !Movement->bOrbitHold)
		{
			FString GroundName = Movement->GroundBody->GetActorNameOrLabel();
			if (const APlanetBody* GroundPlanet = Cast<APlanetBody>(Movement->GroundBody.Get())) { GroundName = GroundPlanet->GetDisplayName(); }
			if (Movement->bLanded)
			{
				DrawText(FString::Printf(TEXT("LANDED  %s   ground %s above sea   (throttle or PgUp lifts off)"), *GroundName.ToUpper(), *FormatDistance(Movement->GroundHeightCm)), Accent, X, Y, Font, UIScale); Y += Line;
			}
			else if (Movement->bTerrainFollow)
			{
				DrawText(FString::Printf(TEXT("TERRAIN FOLLOW  %s   hover %s   ground %s above sea   (PgUp/PgDn height)"), *GroundName.ToUpper(), *FormatDistance(Movement->HoverAltitudeCm), *FormatDistance(Movement->GroundHeightCm)), Accent, X, Y, Font, UIScale); Y += Line;
			}
			else if (Movement->AltitudeAGLCm < 1.0e8)
			{
				DrawText(FString::Printf(TEXT("ALTITUDE %s above ground   ground %s above sea   %s"), *FormatDistance(Movement->AltitudeAGLCm), *FormatDistance(Movement->GroundHeightCm), *GroundName.ToUpper()), Dim, X, Y, Font, UIScale); Y += Line;
			}
		}

		{
			const float SF = Ship->GetShieldFraction();
			const FLinearColor ShieldColor = SF <= 0.f ? FLinearColor(1.f, 0.3f, 0.25f) : (SF < 0.35f ? FLinearColor(1.f, 0.75f, 0.3f) : Accent);
			DrawText(FString::Printf(TEXT("SHIELDS %3.0f%%%s   HULL %3.0f%%"), SF * 100.f, SF <= 0.f ? TEXT(" DOWN") : TEXT(""), Ship->GetHullFraction() * 100.f), ShieldColor, X, Y, Font, UIScale); Y += Line;
			if (Ship->TractorBeam && Ship->TractorBeam->IsBeamActive() && Ship->TractorBeam->Target)
			{
				DrawText(FString::Printf(TEXT("TRACTOR BEAM  %s   range %s   (B releases)"), *Ship->TractorBeam->Target->GetActorNameOrLabel().ToUpper(), *FormatDistance(Ship->TractorBeam->GetRangeCm())), Accent, X, Y, Font, UIScale); Y += Line;
			}
		}
		const FRotator Rot = Ship->GetActorRotation();
		DrawText(FString::Printf(TEXT("HEADING  %03d  PITCH %+03d  ROLL %+03d"), FMath::RoundToInt(FRotator::ClampAxis(Rot.Yaw)), FMath::RoundToInt(Rot.Pitch), FMath::RoundToInt(Rot.Roll)), Dim, X, Y, Font, UIScale); Y += Line;
		DrawText(FString::Printf(TEXT("POSITION %s from the star"), *FormatDistance(Ship->GetUniversePosition().Size())), Dim, X, Y, Font, UIScale); Y += Line * 1.4f;
	}

	if (Warp)
	{
		FString State;
		FLinearColor StateColor = Dim;
		switch (Warp->GetState())
		{
		case EWarpState::Idle: State = TEXT("STANDBY"); break;
		case EWarpState::Charging: State = FString::Printf(TEXT("CHARGING %d%%"), FMath::RoundToInt(Warp->GetChargeProgress() * 100.f)); StateColor = Warning; break;
		case EWarpState::Jumping: State = TEXT("ENGAGING"); StateColor = Warning; break;
		case EWarpState::AtWarp: State = TEXT("AT WARP"); StateColor = Accent; break;
		case EWarpState::Dropping: State = TEXT("DROPPING TO IMPULSE"); StateColor = Warning; break;
		}
		DrawText(FString::Printf(TEXT("WARP %.1f   %s   [%s]"), Warp->WarpFactor, *State, Ship->GetWarpVisualMode() == EWarpVisualMode::Relativistic ? TEXT("RELATIVISTIC") : TEXT("CLASSIC")), StateColor, X, Y, Font, UIScale); Y += Line;
		DrawText(FString::Printf(TEXT("  cruise %s"), *FormatSpeed(Warp->GetWarpSpeedForFactor(Warp->WarpFactor))), Dim, X, Y, SmallFont, UIScale); Y += Line * 1.4f;
	}
	if (Ship->Movement && Ship->Movement->bProximityLimited)
	{
		DrawText(FString::Printf(TEXT("PROXIMITY  %s   closing speed held to %s"), *Ship->Movement->ProximityBodyName.ToUpper(), *FormatSpeed(Ship->Movement->ProximityCap)), Warning, X, Y, Font, UIScale); Y += Line;
	}

	if (Weapons)
	{
		DrawText(TEXT("PHASERS"), Dim, X, Y, Font, UIScale);
		DrawBar(X + 110.f * UIScale, Y + 3.f * UIScale, 160.f * UIScale, 8.f * UIScale, Weapons->GetPhaserEnergy(), Weapons->IsFiringPhasers() ? Warning : FLinearColor(1.f, 0.65f, 0.25f)); Y += Line;
		// One bar per torpedo tube: each reloads on its own.
		DrawText(Weapons->GetNumTubes() > 1 ? TEXT("TUBES") : TEXT("TORPEDO"), Dim, X, Y, Font, UIScale);
		{
			const int32 Tubes = FMath::Max(Weapons->GetNumTubes(), 1);
			const float Gap = 6.f * UIScale;
			const float BarW = (160.f * UIScale - Gap * (Tubes - 1)) / Tubes;
			for (int32 t = 0; t < Tubes; ++t)
			{
				const float Ready = Weapons->GetTubeReadiness(t);
				DrawBar(X + 110.f * UIScale + t * (BarW + Gap), Y + 3.f * UIScale, BarW, 8.f * UIScale, Ready, Ready >= 1.f ? FLinearColor(1.f, 0.3f, 0.1f) : Dim);
			}
			Y += Line;
		}

		if (const AActor* Target = Weapons->LockedTarget)
		{
			const double Dist = FVector::Dist(Target->GetActorLocation(), Ship->GetActorLocation());
			DrawText(FString::Printf(TEXT("TARGET   %s  %s"), *Target->GetActorNameOrLabel(), *FormatDistance(Dist)), Warning, X, Y, Font, UIScale); Y += Line;

			// Target box in screen space.
			FVector Screen = Project(Target->GetActorLocation());
			if (Screen.Z > 0.f)
			{
				const float S = 18.f * UIScale;
				const FLinearColor BoxColor(Warning.R, Warning.G, Warning.B, 0.9f);
				DrawLine(Screen.X - S, Screen.Y - S, Screen.X - S * 0.4f, Screen.Y - S, BoxColor, 1.5f);
				DrawLine(Screen.X - S, Screen.Y - S, Screen.X - S, Screen.Y - S * 0.4f, BoxColor, 1.5f);
				DrawLine(Screen.X + S, Screen.Y - S, Screen.X + S * 0.4f, Screen.Y - S, BoxColor, 1.5f);
				DrawLine(Screen.X + S, Screen.Y - S, Screen.X + S, Screen.Y - S * 0.4f, BoxColor, 1.5f);
				DrawLine(Screen.X - S, Screen.Y + S, Screen.X - S * 0.4f, Screen.Y + S, BoxColor, 1.5f);
				DrawLine(Screen.X - S, Screen.Y + S, Screen.X - S, Screen.Y + S * 0.4f, BoxColor, 1.5f);
				DrawLine(Screen.X + S, Screen.Y + S, Screen.X + S * 0.4f, Screen.Y + S, BoxColor, 1.5f);
				DrawLine(Screen.X + S, Screen.Y + S, Screen.X + S, Screen.Y + S * 0.4f, BoxColor, 1.5f);
			}
		}
		else
		{
			DrawText(TEXT("TARGET   none  (T to lock)"), Dim, X, Y, Font, UIScale); Y += Line;
		}
	}
	DrawText(FString::Printf(TEXT("NAV FILTER   %s   (I cycles)"), *NavFilterName()), NavFilter == ENavFilter::All ? Dim : Accent, X, Y, Font, UIScale); Y += Line;
	DrawScienceRows(Ship, X, Y, Line, UIScale);

	if (bShowNavMarkers)
	{
		DrawNavMarkers(Ship, UIScale);
	}
	if (bShowMinimap)
	{
		DrawMinimap(Ship, UIScale);
	}

	// ---- Right panel: camera + help ----
	const float RX = Canvas->ClipX - 330.f * UIScale;
	float RY = 32.f * UIScale;
	FString CamName;
	switch (Ship->GetCameraMode())
	{
	case EStarshipCameraMode::Chase: CamName = TEXT("CHASE"); break;
	case EStarshipCameraMode::Bridge: CamName = TEXT("BRIDGE"); break;
	case EStarshipCameraMode::Cinematic: CamName = TEXT("CINEMATIC"); break;
	case EStarshipCameraMode::Front: CamName = TEXT("FRONT"); break;
	case EStarshipCameraMode::Port: CamName = TEXT("PORT"); break;
	case EStarshipCameraMode::Starboard: CamName = TEXT("STARBOARD"); break;
	case EStarshipCameraMode::Top: CamName = TEXT("TOP"); break;
	case EStarshipCameraMode::Bottom: CamName = TEXT("BOTTOM"); break;
	}
	DrawText(FString::Printf(TEXT("CAMERA %s%s"), *CamName, Ship->IsFreeLooking() ? TEXT("  (free look)") : TEXT("")), Accent, RX, RY, Font, UIScale); RY += Line * 1.4f;

	if (bShowHelp)
	{
		DrawText(TEXT("Esc  menu      M  star chart      /  science log      H  controls      Tab  HUD"), Dim, RX, RY, SmallFont, UIScale); RY += Line * 0.85f;
	}

	// ---- Centre: warp charge ----
	if (Warp && Warp->GetState() == EWarpState::Charging)
	{
		const float W = 240.f * UIScale;
		DrawBar(Canvas->ClipX * 0.5f - W * 0.5f, Canvas->ClipY * 0.5f + 40.f * UIScale, W, 10.f * UIScale, Warp->GetChargeProgress(), Accent);
	}
}

// ---------------------------------------------------------------------------------------------
// Navigation: markers and minimap
// ---------------------------------------------------------------------------------------------

FLinearColor AStarshipHUD::KindColor(ENavObjectKind Kind) const
{
	switch (Kind)
	{
	case ENavObjectKind::Star: return FLinearColor(1.f, 0.85f, 0.45f);
	case ENavObjectKind::Planet: return FLinearColor(0.55f, 0.85f, 1.f);
	case ENavObjectKind::Moon: return FLinearColor(0.75f, 0.75f, 0.8f);
	case ENavObjectKind::Belt: return FLinearColor(0.7f, 0.6f, 0.5f);
	case ENavObjectKind::Station: return FLinearColor(0.6f, 1.f, 0.7f);
	case ENavObjectKind::Ship: return FLinearColor(1.f, 0.95f, 0.6f);
	case ENavObjectKind::Hostile: return FLinearColor(1.f, 0.3f, 0.25f);
	case ENavObjectKind::Nebula: return FLinearColor(0.9f, 0.55f, 0.95f);
	default: return Warning;
	}
}

void AStarshipHUD::GatherNavObjects(bool bForce)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Walking every actor each frame is wasteful; bodies barely move between refreshes.
	const double Now = World->GetTimeSeconds();
	if (!bForce && NavObjects.Num() > 0 && Now - LastNavGatherTime < 0.25)
	{
		for (FNavObject& O : NavObjects)
		{
			if (O.Actor)
			{
				O.Location = O.Actor->GetActorLocation();
			}
		}
		return;
	}
	LastNavGatherTime = Now;
	NavObjects.Reset();

	bool bHaveStar = false;
	// Every star is listed; the nearest one is the system's sun for the markers.
	const APawn* OwnPawn = GetOwningPlayerController() ? GetOwningPlayerController()->GetPawn() : nullptr;
	const FVector Here = OwnPawn ? OwnPawn->GetActorLocation() : FVector::ZeroVector;
	double BestStarD = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->StarName.IsEmpty() ? It->GetActorNameOrLabel() : It->StarName;
		O.Kind = ENavObjectKind::Star;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetRadiusCm();
		NavObjects.Add(O);
		const double D = FVector::DistSquared(O.Location, Here);
		if (!bHaveStar || D < BestStarD)
		{
			StarLocation = O.Location;
			BestStarD = D;
			bHaveStar = true;
		}
	}
	if (!bHaveStar)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(APlanetBody::StarTag))
			{
				FNavObject O;
				O.Actor = *It;
				O.Label = It->GetActorNameOrLabel();
				O.Kind = ENavObjectKind::Star;
				O.Location = It->GetActorLocation();
				O.RadiusCm = It->GetComponentsBoundingBox().GetExtent().GetMax();
				NavObjects.Add(O);
				StarLocation = O.Location;
				bHaveStar = true;
				break;
			}
		}
	}
	if (!bHaveStar)
	{
		StarLocation = FVector::ZeroVector;
	}

	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetDisplayName();
		O.Kind = It->IsMoon() ? ENavObjectKind::Moon : ENavObjectKind::Planet;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetRadiusCm();
		NavObjects.Add(O);
	}

	for (TActorIterator<ANebula> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetDisplayName();
		O.Kind = ENavObjectKind::Nebula;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetRadiusCm();
		NavObjects.Add(O);
	}

	// Stations, then ships.
	for (TActorIterator<ASpaceStation> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetDisplayName();
		O.Kind = ENavObjectKind::Station;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetBoundingRadiusCm();
		NavObjects.Add(O);
	}
	for (TActorIterator<ABorgCube> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetDisplayName();
		O.Kind = ENavObjectKind::Hostile;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetBoundingRadiusCm();
		NavObjects.Add(O);
	}
	for (TActorIterator<AProceduralStarship> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetDisplayName();
		O.Kind = ENavObjectKind::Ship;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetBoundingRadiusCm();
		NavObjects.Add(O);
	}
	// The player's other craft: from the shuttle, the starship is the way home.
	for (TActorIterator<AStarshipPawn> It(World); It; ++It)
	{
		if (*It == OwnPawn) { continue; }
		FNavObject O;
		O.Actor = *It;
		O.Label = It->GetCraftName();
		O.Kind = ENavObjectKind::Ship;
		O.Location = It->GetActorLocation();
		// No standoff radius: the range shown is to the ship itself, which is what the shuttle pilot steers by.
		O.RadiusCm = 0.0;
		NavObjects.Add(O);
	}

	// The belt field (if the level has one) is the nav target; it parks itself on the belt nearest the ship.
	TActorIterator<ABeltField> FieldIt(World);
	if (FieldIt)
	{
		BeltRadiusCm = FieldIt->BeltRadiusCm;
		FNavObject O;
		O.Actor = *FieldIt;
		O.Label = TEXT("Asteroid belt");
		O.Kind = ENavObjectKind::Belt;
		O.Location = FieldIt->GetActorLocation();
		O.RadiusCm = FieldIt->CellSizeCm;
		NavObjects.Add(O);
		return;
	}

	// Otherwise the belt is the average orbit radius of the PCG belt asteroids, plus the nearest rock as a marker.
	double BeltSum = 0.0;
	int32 BeltCount = 0;
	ASpaceTarget* NearestRock = nullptr;
	double NearestDist = TNumericLimits<double>::Max();
	const AActor* Ship = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	for (TActorIterator<ASpaceTarget> It(World); It; ++It)
	{
		if (!It->GetActorNameOrLabel().StartsWith(TEXT("BeltAsteroid")))
		{
			continue;
		}
		BeltSum += FVector::Dist2D(It->GetActorLocation(), StarLocation);
		++BeltCount;
		if (Ship)
		{
			const double D = FVector::DistSquared(It->GetActorLocation(), Ship->GetActorLocation());
			if (D < NearestDist)
			{
				NearestDist = D;
				NearestRock = *It;
			}
		}
	}
	BeltRadiusCm = BeltCount > 0 ? BeltSum / BeltCount : 0.0;
	if (NearestRock)
	{
		FNavObject O;
		O.Actor = NearestRock;
		O.Label = TEXT("Asteroid belt");
		O.Kind = ENavObjectKind::Belt;
		O.Location = NearestRock->GetActorLocation();
		O.RadiusCm = NearestRock->Radius;
		NavObjects.Add(O);
	}
}

bool AStarshipHUD::SetNavTargetActor(AActor* Actor)
{
	GatherNavObjects(true);
	for (const FNavObject& O : NavObjects)
	{
		if (O.Actor == Actor)
		{
			NavTarget = Actor;
			return true;
		}
	}
	return false;
}

bool AStarshipHUD::SetNavTargetByName(const FString& Name)
{
	GatherNavObjects(true);
	// An exact label wins ("Caerulea" is the giant, not whichever of its moons the list happens to hold first),
	// the nearest of several exact matches (patrol ships share names across systems), then a substring match
	// for shorthand.
	const APawn* OwnPawn = GetOwningPlayerController() ? GetOwningPlayerController()->GetPawn() : nullptr;
	const FVector Here = OwnPawn ? OwnPawn->GetActorLocation() : FVector::ZeroVector;
	AActor* Exact = nullptr;
	double ExactD = TNumericLimits<double>::Max();
	for (const FNavObject& O : NavObjects)
	{
		if (O.Actor && O.Label.Equals(Name, ESearchCase::IgnoreCase))
		{
			const double D = FVector::DistSquared(O.Location, Here);
			if (D < ExactD) { ExactD = D; Exact = O.Actor; }
		}
	}
	if (Exact)
	{
		NavTarget = Exact;
		return true;
	}
	for (const FNavObject& O : NavObjects)
	{
		if (O.Actor && (O.Actor->GetName().Contains(Name) || O.Label.Contains(Name)))
		{
			NavTarget = O.Actor;
			return true;
		}
	}
	return false;
}

void AStarshipHUD::CycleNavTarget()
{
	GatherNavObjects(true);
	if (NavObjects.Num() == 0)
	{
		NavTarget = nullptr;
		return;
	}
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < NavObjects.Num(); ++i)
	{
		if (NavObjects[i].Actor == NavTarget.Get())
		{
			Index = i;
			break;
		}
	}
	// Order: star, planets, moons, belt; wrap to "none" after the last entry. Bodies of other systems are
	// skipped (their star is the way there).
	const APawn* OwnPawn = GetOwningPlayerController() ? GetOwningPlayerController()->GetPawn() : nullptr;
	const FVector Here = OwnPawn ? OwnPawn->GetActorLocation() : FVector::ZeroVector;
	for (int32 Next = Index + 1; Next < NavObjects.Num(); ++Next)
	{
		if (IsNavListed(NavObjects[Next], FVector::Dist(NavObjects[Next].Location, Here)) && PassesNavFilter(NavObjects[Next].Kind))
		{
			NavTarget = NavObjects[Next].Actor;
			return;
		}
	}
	NavTarget = nullptr;
}

bool AStarshipHUD::PassesNavFilter(ENavObjectKind Kind) const
{
	switch (NavFilter)
	{
	case ENavFilter::Stars: return Kind == ENavObjectKind::Star;
	case ENavFilter::Worlds: return Kind == ENavObjectKind::Planet;
	case ENavFilter::Moons: return Kind == ENavObjectKind::Moon;
	case ENavFilter::Stations: return Kind == ENavObjectKind::Station;
	case ENavFilter::Ships: return Kind == ENavObjectKind::Ship || Kind == ENavObjectKind::Hostile;
	case ENavFilter::Nebulae: return Kind == ENavObjectKind::Nebula || Kind == ENavObjectKind::Belt;
	default: return true;
	}
}

void AStarshipHUD::CycleNavFilter()
{
	NavFilter = static_cast<ENavFilter>((static_cast<uint8>(NavFilter) + 1) % 7);
}

bool AStarshipHUD::SetNavFilterByName(const FString& Name)
{
	const FString N = Name.ToLower();
	if (N == TEXT("all") || N == TEXT("off") || N == TEXT("none")) { NavFilter = ENavFilter::All; }
	else if (N.StartsWith(TEXT("star")) || N.StartsWith(TEXT("sun"))) { NavFilter = ENavFilter::Stars; }
	else if (N.StartsWith(TEXT("world")) || N.StartsWith(TEXT("planet")) || N.StartsWith(TEXT("bod"))) { NavFilter = ENavFilter::Worlds; }
	else if (N.StartsWith(TEXT("moon"))) { NavFilter = ENavFilter::Moons; }
	else if (N.StartsWith(TEXT("station")) || N.StartsWith(TEXT("base"))) { NavFilter = ENavFilter::Stations; }
	else if (N.StartsWith(TEXT("ship")) || N.StartsWith(TEXT("vessel"))) { NavFilter = ENavFilter::Ships; }
	else if (N.StartsWith(TEXT("nebula")) || N.StartsWith(TEXT("belt")) || N.StartsWith(TEXT("field"))) { NavFilter = ENavFilter::Nebulae; }
	else { return false; }
	return true;
}

FString AStarshipHUD::NavFilterName() const
{
	switch (NavFilter)
	{
	case ENavFilter::Stars: return TEXT("STARS");
	case ENavFilter::Worlds: return TEXT("WORLDS");
	case ENavFilter::Moons: return TEXT("MOONS");
	case ENavFilter::Stations: return TEXT("STATIONS");
	case ENavFilter::Ships: return TEXT("SHIPS");
	case ENavFilter::Nebulae: return TEXT("NEBULAE & BELTS");
	default: return TEXT("ALL");
	}
}

FLinearColor AStarshipHUD::NavTargetColor() const
{
	const float Pulse = 0.5f + 0.5f * FMath::Sin(NavTime * 4.f);
	return FMath::Lerp(FLinearColor(1.f, 1.f, 1.f, 1.f), FLinearColor(Accent.R, Accent.G, Accent.B, 1.f), Pulse * 0.7f);
}

bool AStarshipHUD::IsNavListed(const FNavObject& O, double Dist) const
{
	return O.Kind == ENavObjectKind::Star || O.Kind == ENavObjectKind::Nebula || Dist < FarSystemCm;
}

void AStarshipHUD::DrawCircle(float CX, float CY, float R, const FLinearColor& Color, int32 Segments, float Thickness)
{
	DrawEllipse(CX, CY, R, R, Color, Segments, Thickness);
}

void AStarshipHUD::DrawArc(float CX, float CY, float R, float FromDeg, float ToDeg, const FLinearColor& Color, int32 Segments, float Thickness)
{
	float PX = CX + R * FMath::Cos(FMath::DegreesToRadians(FromDeg)), PY = CY + R * FMath::Sin(FMath::DegreesToRadians(FromDeg));
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float A = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, (float)i / Segments));
		const float X = CX + R * FMath::Cos(A), Y = CY + R * FMath::Sin(A);
		DrawLine(PX, PY, X, Y, Color, Thickness);
		PX = X; PY = Y;
	}
}

void AStarshipHUD::DrawEllipse(float CX, float CY, float RX, float RY, const FLinearColor& Color, int32 Segments, float Thickness)
{
	float PX = CX + RX, PY = CY;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float A = 2.f * PI * i / Segments;
		const float X = CX + RX * FMath::Cos(A);
		const float Y = CY + RY * FMath::Sin(A);
		DrawLine(PX, PY, X, Y, Color, Thickness);
		PX = X;
		PY = Y;
	}
}

void AStarshipHUD::DrawNavMarkers(const AActor* Ship, float UIScale)
{
	GatherNavObjects();
	if (!Canvas || !Ship)
	{
		return;
	}
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FVector ShipLoc = Ship->GetActorLocation();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float EdgeMargin = 36.f * UIScale;
	NavTime += GetWorld()->GetDeltaSeconds();

	for (const FNavObject& O : NavObjects)
	{
		const bool bIsNavTarget = NavTarget.IsValid() && O.Actor == NavTarget.Get();
		const double Dist = FVector::Dist(O.Location, ShipLoc);
		// Another system's planets and stations all project onto its star from here: only the star is marked.
		if (!bIsNavTarget && !IsNavListed(O, Dist)) { continue; }
		if (!bIsNavTarget && !PassesNavFilter(O.Kind)) { continue; }
		FLinearColor Color = KindColor(O.Kind);
		if (bIsNavTarget)
		{
			Color = NavTargetColor();
		}

		// Project; Project() returns Z > 0 when in front of the camera.
		const FVector Screen = Project(O.Location);
		const bool bInFront = Screen.Z > 0.f;
		const bool bOnScreen = bInFront && Screen.X >= 0.f && Screen.X <= Canvas->ClipX && Screen.Y >= 0.f && Screen.Y <= Canvas->ClipY;

		if (bOnScreen)
		{
			// Size the diamond to the body's angular size, with a minimum so distant bodies stay visible.
			const float Angular = Dist > 0.0 ? (float)(O.RadiusCm / Dist) : 0.f;
			const float S = FMath::Clamp(Angular * Canvas->ClipY * 0.6f, bIsNavTarget ? 14.f * UIScale : 8.f * UIScale, Canvas->ClipY);
			const float Thick = bIsNavTarget ? 2.5f : 1.f;
			DrawLine(Screen.X - S, Screen.Y, Screen.X, Screen.Y - S, Color, Thick);
			DrawLine(Screen.X, Screen.Y - S, Screen.X + S, Screen.Y, Color, Thick);
			DrawLine(Screen.X + S, Screen.Y, Screen.X, Screen.Y + S, Color, Thick);
			DrawLine(Screen.X, Screen.Y + S, Screen.X - S, Screen.Y, Color, Thick);
			const FString Text = FString::Printf(TEXT("%s  %s"), *O.Label, *FormatDistance(Dist));
			if (bIsNavTarget)
			{
				// The nav target's own mark: a broken ring round the diamond with four diagonal ticks, pulsing between
				// white and the accent, and its label in the larger font. Apart from the kind-coloured diamonds, and
				// from the orange corner box of the weapons lock.
				const float R = S * 1.5f + 8.f * UIScale;
				for (int32 q = 0; q < 4; ++q)
				{
					const float Mid = 45.f + 90.f * q;
					DrawArc(Screen.X, Screen.Y, R, Mid - 32.f, Mid + 32.f, Color, 10, 2.f);
					const float A = FMath::DegreesToRadians(Mid);
					DrawLine(Screen.X + R * FMath::Cos(A), Screen.Y + R * FMath::Sin(A), Screen.X + (R + 7.f * UIScale) * FMath::Cos(A), Screen.Y + (R + 7.f * UIScale) * FMath::Sin(A), Color, 2.f);
				}
				DrawText(Text, Color, Screen.X + R + 10.f * UIScale, Screen.Y - 8.f * UIScale, GEngine ? GEngine->GetMediumFont() : SmallFont, UIScale);
				if (IsScanned(O)) { DrawTick(Screen.X + R + 10.f * UIScale, Screen.Y + 12.f * UIScale, 9.f * UIScale, Color); }
			}
			else
			{
				DrawText(Text, Color, Screen.X + S + 4.f * UIScale, Screen.Y - 6.f * UIScale, SmallFont, UIScale);
				if (IsScanned(O)) { DrawTick(Screen.X + S + 4.f * UIScale, Screen.Y + 9.f * UIScale, 7.f * UIScale, Color); }
			}
		}
		else
		{
			// Off-screen: arrow on the screen edge pointing toward it. Behind the camera flips the direction.
			FVector2D Dir(Screen.X - CX, Screen.Y - CY);
			if (!bInFront)
			{
				Dir *= -1.f;
			}
			if (Dir.IsNearlyZero())
			{
				Dir = FVector2D(0.f, -1.f);
			}
			Dir.Normalize();
			// Intersect with the inset rectangle.
			const float HalfW = CX - EdgeMargin, HalfH = CY - EdgeMargin;
			const float TX = FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER ? HalfW / FMath::Abs(Dir.X) : TNumericLimits<float>::Max();
			const float TY = FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER ? HalfH / FMath::Abs(Dir.Y) : TNumericLimits<float>::Max();
			const float Tm = FMath::Min(TX, TY);
			const FVector2D P(CX + Dir.X * Tm, CY + Dir.Y * Tm);
			const FVector2D Perp(-Dir.Y, Dir.X);
			const float A = (bIsNavTarget ? 14.f : 8.f) * UIScale;
			if (bIsNavTarget)
			{
				// Double chevron for the nav target, in its pulsing colour.
				for (int32 k = 0; k < 2; ++k)
				{
					const FVector2D Tip = P + Dir * (A - k * A * 0.6f);
					const FVector2D L = Tip - Dir * A * 0.6f + Perp * A * 0.75f;
					const FVector2D R = Tip - Dir * A * 0.6f - Perp * A * 0.75f;
					DrawLine(Tip.X, Tip.Y, L.X, L.Y, Color, 2.5f);
					DrawLine(Tip.X, Tip.Y, R.X, R.Y, Color, 2.5f);
				}
			}
			else
			{
				const FVector2D Tip = P + Dir * A;
				const FVector2D L = P - Dir * A * 0.4f + Perp * A * 0.6f;
				const FVector2D R = P - Dir * A * 0.4f - Perp * A * 0.6f;
				DrawLine(Tip.X, Tip.Y, L.X, L.Y, Color, 1.5f);
				DrawLine(Tip.X, Tip.Y, R.X, R.Y, Color, 1.5f);
				DrawLine(L.X, L.Y, R.X, R.Y, Color, 1.5f);
			}
			if (bIsNavTarget || O.Kind == ENavObjectKind::Star)
			{
				const FString Text = FString::Printf(TEXT("%s %s"), *O.Label, *FormatDistance(Dist));
				const FVector2D TextPos = P - Dir * (A + 10.f * UIScale);
				DrawText(Text, Color, FMath::Clamp(TextPos.X - 40.f * UIScale, 4.f, Canvas->ClipX - 160.f * UIScale), FMath::Clamp(TextPos.Y - 6.f * UIScale, 4.f, Canvas->ClipY - 16.f * UIScale), SmallFont, UIScale);
			}
		}
	}

	// Nav target readout under the reticle.
	if (NavTarget.IsValid())
	{
		const FNavObject* O = NavObjects.FindByPredicate([this](const FNavObject& N) { return N.Actor == NavTarget.Get(); });
		if (O)
		{
			const double Dist = FVector::Dist(O->Location, ShipLoc) - O->RadiusCm;
			double Speed = 0.0;
			if (const AStarshipPawn* ShipPawn = Cast<AStarshipPawn>(Ship))
			{
				Speed = ShipPawn->Movement ? FVector::DotProduct(ShipPawn->Movement->Velocity, (O->Location - ShipLoc).GetSafeNormal()) : 0.0;
			}
			FString Eta = TEXT("--");
			if (Speed > 1.0)
			{
				const double Seconds = Dist / Speed;
				Eta = Seconds < 90.0 ? FString::Printf(TEXT("%.0f s"), Seconds) : Seconds < 5400.0 ? FString::Printf(TEXT("%.1f min"), Seconds / 60.0) : FString::Printf(TEXT("%.1f h"), Seconds / 3600.0);
			}
			const AStarshipPawn* ShipPawn = Cast<AStarshipPawn>(Ship);
			const TCHAR* Auto = ShipPawn && ShipPawn->AutopilotState == 1 ? TEXT("   AUTOPILOT WARP") : ShipPawn && ShipPawn->AutopilotState == 2 ? TEXT("   AUTOPILOT IMPULSE") : TEXT("");
			const FString Text = FString::Printf(TEXT("NAV  %s   %s   ETA %s%s%s"), *O->Label, *FormatDistance(FMath::Max(Dist, 0.0)), *Eta, Auto, IsScanned(*O) ? TEXT("   catalogued") : TEXT(""));
			DrawText(Text, NavTargetColor(), CX - 120.f * UIScale, CY + 60.f * UIScale, GEngine ? GEngine->GetMediumFont() : nullptr, UIScale);
		}
	}
}

FVector2D AStarshipHUD::MinimapProject(const FVector& Relative, double Scale, float CX, float CY, float YawDeg) const
{
	// Rotate the orbital plane so "up" on the map is north (or the ship's heading), then tilt it.
	const FVector R = FRotator(0.f, -YawDeg, 0.f).RotateVector(Relative);
	const float Tilt = FMath::DegreesToRadians(MinimapTilt);
	const double X = R.Y * Scale;                                   // east to the right
	const double Y = -R.X * Scale * FMath::Cos(Tilt) - R.Z * Scale * FMath::Sin(Tilt); // north up, height lifts the dot
	return FVector2D(CX + X, CY + Y);
}

void AStarshipHUD::DrawMinimap(const AActor* Ship, float UIScale)
{
	if (!Canvas || !Ship)
	{
		return;
	}
	if (!bShowNavMarkers)
	{
		GatherNavObjects();
	}
	UFont* SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;

	const float W = 300.f * UIScale, H = 220.f * UIScale;
	const float X0 = Canvas->ClipX - W - 20.f * UIScale, Y0 = Canvas->ClipY - H - 20.f * UIScale;
	const float CX = X0 + W * 0.5f, CY = Y0 + H * 0.55f;
	Canvas->K2_DrawBox(FVector2D(X0, Y0), FVector2D(W, H), 1.f, FLinearColor(1, 1, 1, 0.25f));
	{
		FCanvasTileItem Bg(FVector2D(X0, Y0), FVector2D(W, H), FLinearColor(0.f, 0.02f, 0.05f, 0.55f));
		Bg.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Bg);
	}

	const FVector ShipLoc = Ship->GetActorLocation();
	// Fit the farthest body (or the ship) with a margin.
	double MaxR = FVector::Dist2D(ShipLoc, StarLocation);
	for (const FNavObject& O : NavObjects)
	{
		MaxR = FMath::Max(MaxR, FVector::Dist2D(O.Location, StarLocation));
	}
	MaxR = FMath::Max(MaxR, BeltRadiusCm);
	MaxR = FMath::Max(MaxR, 1.0);
	const double Scale = (W * 0.46) / MaxR;
	const float YawDeg = bMinimapHeadingUp ? Ship->GetActorRotation().Yaw : 0.f;
	const float Tilt = FMath::DegreesToRadians(MinimapTilt);

	// Orbit rings: one per distinct planet orbit radius, plus the belt.
	TArray<double> Orbits;
	for (const FNavObject& O : NavObjects)
	{
		if (O.Kind != ENavObjectKind::Planet)
		{
			continue;
		}
		const double R = FVector::Dist2D(O.Location, StarLocation);
		bool bDup = false;
		for (double E : Orbits)
		{
			if (FMath::Abs(E - R) < R * 0.05)
			{
				bDup = true;
				break;
			}
		}
		if (!bDup)
		{
			Orbits.Add(R);
		}
	}
	const FLinearColor OrbitColor(0.4f, 0.6f, 0.8f, 0.35f);
	for (double R : Orbits)
	{
		DrawEllipse(CX, CY, R * Scale, R * Scale * FMath::Cos(Tilt), OrbitColor, 64, 1.f);
	}
	if (BeltRadiusCm > 0.0)
	{
		const FLinearColor BeltColor(0.7f, 0.6f, 0.5f, 0.5f);
		// Dashed: draw every other segment.
		const int32 Segments = 72;
		for (int32 i = 0; i < Segments; i += 2)
		{
			const float A0 = 2.f * PI * i / Segments, A1 = 2.f * PI * (i + 1) / Segments;
			DrawLine(CX + BeltRadiusCm * Scale * FMath::Cos(A0), CY + BeltRadiusCm * Scale * FMath::Cos(Tilt) * FMath::Sin(A0),
				CX + BeltRadiusCm * Scale * FMath::Cos(A1), CY + BeltRadiusCm * Scale * FMath::Cos(Tilt) * FMath::Sin(A1), BeltColor, 2.f);
		}
	}

	// Bodies: a vertical stem from the orbital plane shows height; dot size follows radius.
	for (const FNavObject& O : NavObjects)
	{
		if (O.Kind == ENavObjectKind::Belt)
		{
			continue;
		}
		const FVector Rel = O.Location - StarLocation;
		const FVector2D P = MinimapProject(Rel, Scale, CX, CY, YawDeg);
		const FVector2D Base = MinimapProject(FVector(Rel.X, Rel.Y, 0.0), Scale, CX, CY, YawDeg);
		const bool bIsNavTarget = NavTarget.IsValid() && O.Actor == NavTarget.Get();
		// The filter applies here too; the suns stay as the map's frame.
		if (!bIsNavTarget && O.Kind != ENavObjectKind::Star && !PassesNavFilter(O.Kind)) { continue; }
		FLinearColor Color = KindColor(O.Kind);
		float Dot = O.Kind == ENavObjectKind::Star ? 5.f : O.Kind == ENavObjectKind::Moon ? 1.5f : 3.f;
		Dot *= UIScale;
		if (FMath::Abs(P.Y - Base.Y) > 1.f)
		{
			DrawLine(Base.X, Base.Y, P.X, P.Y, FLinearColor(Color.R, Color.G, Color.B, 0.3f), 1.f);
		}
		if (bIsNavTarget)
		{
			DrawCircle(P.X, P.Y, Dot + 5.f * UIScale, NavTargetColor(), 16, 2.f);
		}
		Canvas->K2_DrawPolygon(nullptr, P, FVector2D(Dot, Dot), 12, Color);
		if (bIsNavTarget && O.Kind != ENavObjectKind::Star && O.Kind != ENavObjectKind::Planet)
		{
			DrawText(O.Label, NavTargetColor(), P.X + Dot + 6.f * UIScale, P.Y - 6.f * UIScale, SmallFont, UIScale * 0.85f);
		}
		if (O.Kind == ENavObjectKind::Star || O.Kind == ENavObjectKind::Planet)
		{
			DrawText(O.Label, FLinearColor(Color.R, Color.G, Color.B, 0.8f), P.X + Dot + 2.f, P.Y - 6.f * UIScale, SmallFont, UIScale * 0.85f);
		}
	}

	// The ship: blinking dot with a heading tick and its own stem.
	{
		const FVector Rel = ShipLoc - StarLocation;
		const FVector2D P = MinimapProject(Rel, Scale, CX, CY, YawDeg);
		const FVector2D Base = MinimapProject(FVector(Rel.X, Rel.Y, 0.0), Scale, CX, CY, YawDeg);
		const FVector Fwd = Ship->GetActorForwardVector();
		const FVector2D Tip = MinimapProject(Rel + Fwd * (MaxR * 0.08), Scale, CX, CY, YawDeg);
		const float Blink = 0.6f + 0.4f * FMath::Sin(NavTime * 8.f);
		const FLinearColor ShipColor(Accent.R, Accent.G, Accent.B, Blink);
		DrawLine(Base.X, Base.Y, P.X, P.Y, FLinearColor(Accent.R, Accent.G, Accent.B, 0.35f), 1.f);
		DrawLine(P.X, P.Y, Tip.X, Tip.Y, ShipColor, 1.5f);
		Canvas->K2_DrawPolygon(nullptr, P, FVector2D(3.f * UIScale, 3.f * UIScale), 10, ShipColor);
		DrawText(TEXT("YOU"), ShipColor, P.X + 5.f * UIScale, P.Y + 2.f * UIScale, SmallFont, UIScale * 0.85f);
	}

	// Scale legend.
	DrawText(FString::Printf(TEXT("SYSTEM MAP  radius %s"), *FormatDistance(MaxR)), FLinearColor(1, 1, 1, 0.6f), X0 + 6.f * UIScale, Y0 + 4.f * UIScale, SmallFont, UIScale * 0.85f);
}

bool AStarshipHUD::GetNavTargetInfo(FNavObject& Out)
{
	if (!NavTarget.IsValid())
	{
		return false;
	}
	GatherNavObjects(true);
	for (const FNavObject& O : NavObjects)
	{
		if (O.Actor == NavTarget.Get())
		{
			Out = O;
			return true;
		}
	}
	return false;
}

void AStarshipHUD::DrawLoadingScreen(const USlonkLoadSubsystem& Load, UFont* Font, UFont* SmallFont, float UIScale)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const FLinearColor Teal(0.30f, 0.78f, 0.82f, 1.f);
	const FLinearColor Orange(1.0f, 0.62f, 0.22f, 1.f);
	const FLinearColor Pale(0.86f, 0.88f, 0.92f, 1.f);
	const FLinearColor Faint(0.86f, 0.88f, 0.92f, 0.38f);
	const FLinearColor Panel(0.012f, 0.016f, 0.028f, 1.f);
	DrawRect(Panel, 0.f, 0.f, W, H);

	auto Centred = [&](const FString& Text, float Y, const FLinearColor& Color, UFont* F, float Scale)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Text, TW, TH, F, Scale);
		DrawText(Text, Color, (W - TW) * 0.5f, Y, F, Scale);
		return TH;
	};

	// The Okudagram frame: a wide black panel with teal rails top and bottom and an orange tab at the left.
	const float PW = FMath::Min(W * 0.62f, 980.f * UIScale);
	const float PX = (W - PW) * 0.5f;
	const float Top = H * 0.30f;
	const float Rail = 6.f * UIScale;
	DrawRect(Teal, PX, Top, PW, Rail);
	DrawRect(Orange, PX, Top, 120.f * UIScale, Rail);
	DrawRect(Teal, PX, H * 0.70f, PW, Rail);
	DrawRect(Orange, PX + PW - 120.f * UIScale, H * 0.70f, 120.f * UIScale, Rail);

	float Y = Top + 34.f * UIScale;
	Y += Centred(TEXT("U.S.S. ENTERPRISE   NCC-1701-A"), Y, Pale, Font, 2.1f * UIScale) + 8.f * UIScale;
	Y += Centred(TEXT("STARFLEET SECTOR DATABASE   ·   INITIALISING STELLAR CARTOGRAPHY"), Y, Teal, SmallFont, 1.25f * UIScale) + 40.f * UIScale;

	// Progress bar with a percentage, segmented like a console readout.
	const float BarW = PW * 0.84f, BarH = 20.f * UIScale;
	const float BX = (W - BarW) * 0.5f;
	const float P = Load.GetProgress();
	DrawRect(FLinearColor(0.08f, 0.12f, 0.18f, 1.f), BX, Y, BarW, BarH);
	const int32 Segments = 40;
	const float SegW = BarW / Segments;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Fill = FMath::Clamp(P * Segments - i, 0.f, 1.f);
		if (Fill > 0.f)
		{
			DrawRect(Orange, BX + i * SegW + 1.f, Y + 2.f, (SegW - 2.f) * Fill, BarH - 4.f);
		}
	}
	DrawText(FString::Printf(TEXT("%3d%%"), FMath::RoundToInt(P * 100.f)), Pale, BX + BarW + 14.f * UIScale, Y - 2.f * UIScale, Font, 1.2f * UIScale);
	Y += BarH + 18.f * UIScale;

	// What is being built now, then the last few things finished, dimming with age.
	const FString Now = Load.GetCurrentLabel().IsEmpty() ? FString(TEXT("Stand by")) : Load.GetCurrentLabel();
	DrawText(Now.ToUpper(), Teal, BX, Y, Font, 1.1f * UIScale);
	DrawText(FString::Printf(TEXT("%d / %d"), Load.GetTasksDone(), Load.GetTasksTotal()), Faint, BX + BarW - 90.f * UIScale, Y, Font, 1.1f * UIScale);
	Y += 26.f * UIScale;
	const TArray<FString>& Recent = Load.GetRecentLabels();
	for (int32 i = Recent.Num() - 1, Row = 0; i >= 0 && Row < 5; --i, ++Row)
	{
		FLinearColor C = Faint;
		C.A *= 1.f - 0.16f * Row;
		DrawText(Recent[i].ToUpper(), C, BX, Y, SmallFont, 1.05f * UIScale);
		Y += 17.f * UIScale;
	}
	DrawText(FString::Printf(TEXT("T+ %5.1f s"), Load.GetElapsedSeconds()), Faint, PX + PW - 110.f * UIScale, H * 0.70f - 24.f * UIScale, SmallFont, 1.1f * UIScale);
}

// ---------------------------------------------------------------------------------------------------------------------
// Science

UScienceLogSubsystem* AStarshipHUD::ScienceLog() const
{
	const UWorld* World = GetWorld();
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
}

bool AStarshipHUD::IsScanned(const FNavObject& O) const
{
	const UScienceLogSubsystem* Log = ScienceLog();
	return Log && O.Actor && Log->IsScanned(UScienceScannerComponent::KeyFor(O.Actor, O.Label));
}

void AStarshipHUD::DrawTick(float X, float Y, float Size, const FLinearColor& Color)
{
	DrawLine(X, Y, X + Size * 0.4f, Y + Size * 0.5f, Color, 1.5f);
	DrawLine(X + Size * 0.4f, Y + Size * 0.5f, X + Size, Y - Size * 0.5f, Color, 1.5f);
}

void AStarshipHUD::DrawScienceRows(const AStarshipPawn* Ship, float& X, float& Y, float Line, float UIScale)
{
	UScienceLogSubsystem* Log = ScienceLog();
	if (!Log || !Ship || !Canvas) { return; }
	UFont* Font = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.75f);

	FString System; TArray<FString> Keys;
	UScienceScannerComponent::SurveyKeys(GetWorld(), Ship->GetActorLocation(), System, Keys);
	const FString Survey = Keys.Num() > 0 ? FString::Printf(TEXT("   %s survey %d/%d"), *System.ToUpper(), Log->CountScanned(Keys), Keys.Num()) : FString();
	DrawText(FString::Printf(TEXT("SCIENCE  %d pts   %s%s"), Log->TotalPoints(), *Log->RatingTitle().ToUpper(), *Survey), Dim, X, Y, Font, UIScale); Y += Line;

	const UScienceScannerComponent* Scanner = Ship->Scanner;
	if (Scanner && Scanner->IsBusy())
	{
		const bool bHeld = Scanner->GetState() == EScanState::Held;
		const FLinearColor C = bHeld ? Warning : Accent;
		DrawText(bHeld ? TEXT("SCAN HELD") : TEXT("SCANNING"), C, X, Y, Font, UIScale);
		DrawBar(X + 110.f * UIScale, Y + 3.f * UIScale, 160.f * UIScale, 8.f * UIScale, Scanner->GetProgress(), C);
		DrawText(FString::Printf(TEXT("%s  %d%%%s"), *Scanner->GetTargetLabel(), FMath::RoundToInt(Scanner->GetProgress() * 100.f), bHeld ? *(TEXT("   ") + Scanner->GetStatusText()) : TEXT("")), C, X + 280.f * UIScale, Y, Font, UIScale);
		Y += Line;
		return;
	}

	// Idle: say what Enter would scan and whether it is in range.
	if (Ship->Movement && Ship->Movement->bLanded)
	{
		DrawText(TEXT("SCAN     Enter surveys the landing site"), Dim, X, Y, Font, UIScale); Y += Line;
		return;
	}
	AActor* Candidate = Ship->Weapons ? Ship->Weapons->LockedTarget.Get() : nullptr;
	FString Label = Candidate ? Candidate->GetActorNameOrLabel() : FString();
	if (const ASpaceVessel* V = Cast<ASpaceVessel>(Candidate)) { Label = V->GetDisplayName(); }
	if (!Candidate)
	{
		FNavObject Info;
		if (GetNavTargetInfo(Info)) { Candidate = Info.Actor; Label = Info.Label; }
	}
	EScanKind Kind;
	if (Scanner && Candidate && UScienceScannerComponent::ClassifyTarget(Candidate, Kind))
	{
		double Dist, Range;
		const bool bIn = Scanner->InRange(Candidate, Kind, Dist, Range);
		const bool bDone = Log->IsScanned(UScienceScannerComponent::KeyFor(Candidate, Label));
		FString Text;
		if (bDone) { Text = FString::Printf(TEXT("SCAN     %s is catalogued"), *Label); }
		else if (bIn) { Text = FString::Printf(TEXT("SCAN     %s in sensor range: Enter"), *Label); }
		else { Text = FString::Printf(TEXT("SCAN     %s: close to %s (now %s)"), *Label, *FormatDistance(Range), *FormatDistance(FMath::Max(Dist, 0.0))); }
		DrawText(Text, bIn && !bDone ? Accent : Dim, X, Y, Font, UIScale); Y += Line;
	}
}
