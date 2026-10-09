#include "StarshipHUD.h"

#include "Engine/Canvas.h"
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

		const FRotator Rot = Ship->GetActorRotation();
		DrawText(FString::Printf(TEXT("HEADING  %03d  PITCH %+03d  ROLL %+03d"), FMath::RoundToInt(FRotator::ClampAxis(Rot.Yaw)), FMath::RoundToInt(Rot.Pitch), FMath::RoundToInt(Rot.Roll)), Dim, X, Y, Font, UIScale); Y += Line;
		DrawText(FString::Printf(TEXT("POSITION %s from origin"), *FormatDistance(Ship->GetActorLocation().Size())), Dim, X, Y, Font, UIScale); Y += Line * 1.4f;
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
		DrawText(FString::Printf(TEXT("WARP %.1f   %s"), Warp->WarpFactor, *State), StateColor, X, Y, Font, UIScale); Y += Line;
		DrawText(FString::Printf(TEXT("  cruise %s"), *FormatSpeed(Warp->GetWarpSpeedForFactor(Warp->WarpFactor))), Dim, X, Y, SmallFont, UIScale); Y += Line * 1.4f;
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
			TEXT("J / Y button         engage / disengage warp"),
			TEXT("[ ]  or  - +         warp factor"),
			TEXT("LMB / F / RT         phasers (hold)"),
			TEXT("Space / G / A        photon torpedo"),
			TEXT("T  cycle target      Y  clear target"),
			TEXT("V  camera            RMB hold  free look"),
			TEXT("R  reset camera      Tab  HUD     H  help"),
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
