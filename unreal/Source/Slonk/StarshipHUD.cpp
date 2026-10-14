#include "StarshipHUD.h"

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
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "StarshipWeaponsComponent.h"
#include "WarpDriveComponent.h"

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
	DrawText(TEXT("U.S.S. ENTERPRISE  NCC-1701-A"), Accent, X, Y, Font, UIScale); Y += Line * 1.4f;

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
			ThrottleLabel = TEXT("WARP");
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
		DrawText(TEXT("TORPEDO"), Dim, X, Y, Font, UIScale);
		const float Ready = Weapons->GetTorpedoReadiness();
		DrawBar(X + 110.f * UIScale, Y + 3.f * UIScale, 160.f * UIScale, 8.f * UIScale, Ready, Ready >= 1.f ? FLinearColor(1.f, 0.3f, 0.1f) : Dim); Y += Line;

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
	}
	DrawText(FString::Printf(TEXT("CAMERA %s%s"), *CamName, Ship->IsFreeLooking() ? TEXT("  (free look)") : TEXT("")), Accent, RX, RY, Font, UIScale); RY += Line * 1.4f;

	if (bShowHelp)
	{
		const TCHAR* Help[] = {
			TEXT("Mouse / left stick   steer"),
			TEXT("A D / bumpers        roll      Q E  yaw"),
			TEXT("W S / wheel          throttle"),
			TEXT("1 2 3 4              1/4 .. full impulse"),
			TEXT("X or 0  all stop     Z  reverse"),
			TEXT("PgUp PgDn            vertical thrusters"),
			TEXT("O  orbit hold around nav target / nearest body (follows ships)"),
			TEXT("B  tractor beam on the locked target (else the nav target)"),
			TEXT("J / Y button         engage / disengage warp"),
			TEXT("[ ]  or  - +         warp factor"),
			TEXT("LMB / F / RT         phasers (hold)"),
			TEXT("Space / G / A        photon torpedo"),
			TEXT("T  cycle target      Y  clear target"),
			TEXT("V  camera            RMB hold  free look"),
			TEXT("RMB + wheel  zoom    R  reset camera/zoom"),
			TEXT("Tab  HUD             H  help"),
			TEXT("N  nav markers       M  minimap"),
			TEXT("L  next nav target   K  clear nav target"),
			TEXT("Home  jump to nav target (dev)"),
			TEXT("U  warp visuals: classic / relativistic"),
		};
		for (const TCHAR* L : Help)
		{
			DrawText(L, Dim, RX, RY, SmallFont, UIScale); RY += Line * 0.85f;
		}
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
	for (TActorIterator<AStarBody> It(World); It; ++It)
	{
		FNavObject O;
		O.Actor = *It;
		O.Label = It->StarName.IsEmpty() ? It->GetActorNameOrLabel() : It->StarName;
		O.Kind = ENavObjectKind::Star;
		O.Location = It->GetActorLocation();
		O.RadiusCm = It->GetRadiusCm();
		NavObjects.Add(O);
		if (!bHaveStar)
		{
			StarLocation = O.Location;
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
	// Order: star, planets, moons, belt; wrap to "none" after the last entry.
	const int32 Next = Index + 1;
	NavTarget = Next < NavObjects.Num() ? NavObjects[Next].Actor : nullptr;
}

void AStarshipHUD::DrawCircle(float CX, float CY, float R, const FLinearColor& Color, int32 Segments, float Thickness)
{
	DrawEllipse(CX, CY, R, R, Color, Segments, Thickness);
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
		FLinearColor Color = KindColor(O.Kind);
		if (bIsNavTarget)
		{
			Color = FLinearColor(1.f, 1.f, 1.f, 0.9f + 0.1f * FMath::Sin(NavTime * 6.f));
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
			DrawLine(Screen.X - S, Screen.Y, Screen.X, Screen.Y - S, Color, bIsNavTarget ? 2.f : 1.f);
			DrawLine(Screen.X, Screen.Y - S, Screen.X + S, Screen.Y, Color, bIsNavTarget ? 2.f : 1.f);
			DrawLine(Screen.X + S, Screen.Y, Screen.X, Screen.Y + S, Color, bIsNavTarget ? 2.f : 1.f);
			DrawLine(Screen.X, Screen.Y + S, Screen.X - S, Screen.Y, Color, bIsNavTarget ? 2.f : 1.f);
			const FString Text = FString::Printf(TEXT("%s  %s"), *O.Label, *FormatDistance(Dist));
			DrawText(Text, Color, Screen.X + S + 4.f * UIScale, Screen.Y - 6.f * UIScale, SmallFont, UIScale);
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
			const float A = (bIsNavTarget ? 12.f : 8.f) * UIScale;
			const FVector2D Tip = P + Dir * A;
			const FVector2D L = P - Dir * A * 0.4f + Perp * A * 0.6f;
			const FVector2D R = P - Dir * A * 0.4f - Perp * A * 0.6f;
			DrawLine(Tip.X, Tip.Y, L.X, L.Y, Color, 1.5f);
			DrawLine(Tip.X, Tip.Y, R.X, R.Y, Color, 1.5f);
			DrawLine(L.X, L.Y, R.X, R.Y, Color, 1.5f);
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
			const FString Text = FString::Printf(TEXT("NAV  %s   %s   ETA %s"), *O->Label, *FormatDistance(FMath::Max(Dist, 0.0)), *Eta);
			DrawText(Text, FLinearColor::White, CX - 120.f * UIScale, CY + 60.f * UIScale, GEngine ? GEngine->GetMediumFont() : nullptr, UIScale);
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
		FLinearColor Color = KindColor(O.Kind);
		float Dot = O.Kind == ENavObjectKind::Star ? 5.f : O.Kind == ENavObjectKind::Moon ? 1.5f : 3.f;
		Dot *= UIScale;
		if (FMath::Abs(P.Y - Base.Y) > 1.f)
		{
			DrawLine(Base.X, Base.Y, P.X, P.Y, FLinearColor(Color.R, Color.G, Color.B, 0.3f), 1.f);
		}
		if (bIsNavTarget)
		{
			DrawCircle(P.X, P.Y, Dot + 4.f * UIScale, FLinearColor::White, 16, 1.5f);
		}
		Canvas->K2_DrawPolygon(nullptr, P, FVector2D(Dot, Dot), 12, Color);
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
