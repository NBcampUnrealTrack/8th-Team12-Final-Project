// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/ULightingService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyLighting, Log, All);

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"

// Light actor headers
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"

// Light component headers
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"

// Post-process, fog, reflection, lightmass
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ReflectionCapture.h"
#include "Engine/SphereReflectionCapture.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Components/SphereReflectionCaptureComponent.h"
#include "Lightmass/LightmassImportanceVolume.h"

#include "Engine/Texture.h"
#include "Runtime/Launch/Resources/Version.h"

// =================================================================
// Helpers
// =================================================================

FString ULightingService::ApplyActorLabel(AActor* Actor, const FString& DesiredName)
{
	if (!Actor)
	{
		return FString();
	}
	if (!DesiredName.IsEmpty())
	{
		Actor->SetActorLabel(DesiredName);
	}
	return Actor->GetActorLabel();
}

// =================================================================
// Directional / Sun Lighting
// =================================================================

FString ULightingService::SpawnDirectionalLight(
	const FString& Name,
	FVector Location,
	FRotator Rotation,
	FLinearColor Color,
	float Intensity)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnDirectionalLight: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Directional Light")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetRotation(Rotation.Quaternion());
	SpawnT.SetScale3D(FVector::OneVector);

	ADirectionalLight* NewActor = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnDirectionalLight: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (ULightComponent* LightComp = NewActor->GetLightComponent())
	{
		LightComp->Modify();
		LightComp->SetIntensity(Intensity);
		LightComp->SetLightColor(Color);
	}

	return ApplyActorLabel(NewActor, Name);
}

bool ULightingService::SetDirectionalLightAsAtmosphereSun(const FString& ActorNameOrPath, int32 SunIndex)
{
	ADirectionalLight* DirLight = Cast<ADirectionalLight>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!DirLight)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetDirectionalLightAsAtmosphereSun: '%s' is not a DirectionalLight"), *ActorNameOrPath);
		return false;
	}

	UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(DirLight->GetLightComponent());
	if (!Comp)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetDirectionalLightAsAtmosphereSun: no DirectionalLightComponent on '%s'"), *ActorNameOrPath);
		return false;
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Set Atmosphere Sun Light")));
	Comp->Modify();
	Comp->SetAtmosphereSunLight(true);
	Comp->SetAtmosphereSunLightIndex(SunIndex);
	Comp->MarkRenderStateDirty();
	return true;
}

// =================================================================
// Sky Light
// =================================================================

FString ULightingService::SpawnSkyLight(const FString& Name, FVector Location)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSkyLight: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Sky Light")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);

	ASkyLight* NewActor = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSkyLight: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	return ApplyActorLabel(NewActor, Name);
}

bool ULightingService::RecaptureSky(const FString& ActorNameOrPath)
{
	ASkyLight* SkyLight = Cast<ASkyLight>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!SkyLight)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("RecaptureSky: '%s' is not a SkyLight"), *ActorNameOrPath);
		return false;
	}

	USkyLightComponent* Comp = SkyLight->GetLightComponent();
	if (!Comp)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("RecaptureSky: no SkyLightComponent on '%s'"), *ActorNameOrPath);
		return false;
	}

	Comp->RecaptureSky();
	return true;
}

bool ULightingService::SetSkyLightRealTimeCapture(const FString& ActorNameOrPath, bool bEnabled)
{
	ASkyLight* SkyLight = Cast<ASkyLight>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!SkyLight)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetSkyLightRealTimeCapture: '%s' is not a SkyLight"), *ActorNameOrPath);
		return false;
	}

	USkyLightComponent* Comp = SkyLight->GetLightComponent();
	if (!Comp)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetSkyLightRealTimeCapture: no SkyLightComponent on '%s'"), *ActorNameOrPath);
		return false;
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Set Sky Light Real Time Capture")));
	Comp->Modify();
	Comp->bRealTimeCapture = bEnabled;
	Comp->MarkRenderStateDirty();
	return true;
}

// =================================================================
// Point / Spot / Rect Lights
// =================================================================

FString ULightingService::SpawnPointLight(
	const FString& Name,
	FVector Location,
	FLinearColor Color,
	float Intensity,
	float AttenuationRadius)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnPointLight: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Point Light")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);

	APointLight* NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnPointLight: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (UPointLightComponent* LC = Cast<UPointLightComponent>(NewActor->GetLightComponent()))
	{
		LC->Modify();
		LC->SetIntensity(Intensity);
		LC->SetLightColor(Color);
		LC->SetAttenuationRadius(AttenuationRadius);
	}

	return ApplyActorLabel(NewActor, Name);
}

FString ULightingService::SpawnSpotLight(
	const FString& Name,
	FVector Location,
	FRotator Rotation,
	FLinearColor Color,
	float Intensity,
	float InnerConeAngle,
	float OuterConeAngle)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSpotLight: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Spot Light")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetRotation(Rotation.Quaternion());

	ASpotLight* NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSpotLight: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (USpotLightComponent* LC = Cast<USpotLightComponent>(NewActor->GetLightComponent()))
	{
		LC->Modify();
		LC->SetIntensity(Intensity);
		LC->SetLightColor(Color);
		LC->SetInnerConeAngle(InnerConeAngle);
		LC->SetOuterConeAngle(OuterConeAngle);
	}

	return ApplyActorLabel(NewActor, Name);
}

FString ULightingService::SpawnRectLight(
	const FString& Name,
	FVector Location,
	FRotator Rotation,
	FLinearColor Color,
	float Intensity,
	float SourceWidth,
	float SourceHeight)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnRectLight: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Rect Light")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetRotation(Rotation.Quaternion());

	ARectLight* NewActor = World->SpawnActor<ARectLight>(ARectLight::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnRectLight: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (URectLightComponent* LC = Cast<URectLightComponent>(NewActor->GetLightComponent()))
	{
		LC->Modify();
		LC->SetIntensity(Intensity);
		LC->SetLightColor(Color);
		LC->SetSourceWidth(SourceWidth);
		LC->SetSourceHeight(SourceHeight);
	}

	return ApplyActorLabel(NewActor, Name);
}

bool ULightingService::SetRectLightSourceTexture(const FString& ActorNameOrPath, const FString& TextureAssetPath)
{
	ARectLight* RectLight = Cast<ARectLight>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!RectLight)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetRectLightSourceTexture: '%s' is not a RectLight"), *ActorNameOrPath);
		return false;
	}

	URectLightComponent* Comp = Cast<URectLightComponent>(RectLight->GetLightComponent());
	if (!Comp)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetRectLightSourceTexture: no RectLightComponent on '%s'"), *ActorNameOrPath);
		return false;
	}

	UTexture* Tex = LoadObject<UTexture>(nullptr, *TextureAssetPath);
	if (!Tex)
	{
		// Fall back to EditorAssetLibrary in case the path needs cooking-style resolution.
		if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(TextureAssetPath))
		{
			Tex = Cast<UTexture>(Loaded);
		}
	}
	if (!Tex)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetRectLightSourceTexture: failed to load texture '%s'"), *TextureAssetPath);
		return false;
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Set Rect Light Source Texture")));
	Comp->Modify();
	Comp->SetSourceTexture(Tex);
	Comp->MarkRenderStateDirty();
	return true;
}

// =================================================================
// Post Process
// =================================================================

FString ULightingService::SpawnPostProcessVolume(
	const FString& Name,
	FVector Location,
	FVector Extent,
	bool bUnbound)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnPostProcessVolume: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Post Process Volume")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Default box brush is ±100 units. Convert requested extent to actor scale.
	const FVector SafeExtent(
		FMath::Max(Extent.X, 1.0f),
		FMath::Max(Extent.Y, 1.0f),
		FMath::Max(Extent.Z, 1.0f));

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetScale3D(SafeExtent / 100.0f);

	APostProcessVolume* NewActor = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnPostProcessVolume: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	NewActor->bUnbound = bUnbound;

	return ApplyActorLabel(NewActor, Name);
}

bool ULightingService::SetPostProcessExposureBias(const FString& ActorNameOrPath, float ExposureBias)
{
	APostProcessVolume* Volume = Cast<APostProcessVolume>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!Volume)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetPostProcessExposureBias: '%s' is not a PostProcessVolume"), *ActorNameOrPath);
		return false;
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Set Post Process Exposure Bias")));
	Volume->Modify();
	Volume->Settings.bOverride_AutoExposureBias = true;
	Volume->Settings.AutoExposureBias = ExposureBias;
	return true;
}

bool ULightingService::SetPostProcessBloomIntensity(const FString& ActorNameOrPath, float BloomIntensity)
{
	APostProcessVolume* Volume = Cast<APostProcessVolume>(URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath));
	if (!Volume)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SetPostProcessBloomIntensity: '%s' is not a PostProcessVolume"), *ActorNameOrPath);
		return false;
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Set Post Process Bloom Intensity")));
	Volume->Modify();
	Volume->Settings.bOverride_BloomIntensity = true;
	Volume->Settings.BloomIntensity = BloomIntensity;
	return true;
}

// =================================================================
// Fog
// =================================================================

FString ULightingService::SpawnExponentialHeightFog(
	const FString& Name,
	FVector Location,
	float FogDensity,
	float HeightFalloff)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnExponentialHeightFog: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Exponential Height Fog")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);

	AExponentialHeightFog* NewActor = World->SpawnActor<AExponentialHeightFog>(
		AExponentialHeightFog::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnExponentialHeightFog: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (UExponentialHeightFogComponent* Comp = NewActor->FindComponentByClass<UExponentialHeightFogComponent>())
	{
		Comp->Modify();
		Comp->FogDensity = FogDensity;
		Comp->FogHeightFalloff = HeightFalloff;
		Comp->MarkRenderStateDirty();
	}

	return ApplyActorLabel(NewActor, Name);
}

FString ULightingService::SpawnLocalFogVolume(const FString& Name, FVector Location, float FogDensity)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnLocalFogVolume: no editor world available"));
		return FString();
	}

	// Look up the class dynamically so this file compiles even if the LocalFogVolume
	// header path is not on the include search path in this configuration.
	UClass* FogClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.LocalFogVolume"));
	if (!FogClass)
	{
		UE_LOG(LogRevvyLighting, Warning,
			TEXT("SpawnLocalFogVolume: LocalFogVolume class not available in this build"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Local Fog Volume")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);

	AActor* NewActor = World->SpawnActor<AActor>(FogClass, SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnLocalFogVolume: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();

	// Find the LocalFogVolumeComponent and set density via reflection. The exact
	// property name varies across engine versions (HeightFogExtinction,
	// RadialFogExtinction, FogDensity) so try them in priority order.
	static const TCHAR* DensityFieldNames[] = {
		TEXT("HeightFogExtinction"),
		TEXT("RadialFogExtinction"),
		TEXT("FogDensity")
	};

	for (UActorComponent* AC : NewActor->GetComponents())
	{
		if (!AC) continue;
		UClass* CC = AC->GetClass();
		if (!CC || !CC->GetName().Contains(TEXT("LocalFogVolumeComponent"))) continue;

		AC->Modify();
		for (const TCHAR* FieldName : DensityFieldNames)
		{
			if (FFloatProperty* Prop = CastField<FFloatProperty>(CC->FindPropertyByName(FName(FieldName))))
			{
				Prop->SetPropertyValue_InContainer(AC, FogDensity);
				break;
			}
		}
		AC->MarkRenderStateDirty();
		break;
	}

	return ApplyActorLabel(NewActor, Name);
#else
	UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnLocalFogVolume: requires UE 5.5 or newer"));
	return FString();
#endif
}

// =================================================================
// Reflection Capture
// =================================================================

FString ULightingService::SpawnSphereReflectionCapture(
	const FString& Name,
	FVector Location,
	float InfluenceRadius)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSphereReflectionCapture: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Sphere Reflection Capture")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);

	ASphereReflectionCapture* NewActor = World->SpawnActor<ASphereReflectionCapture>(
		ASphereReflectionCapture::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnSphereReflectionCapture: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	if (USphereReflectionCaptureComponent* Comp = NewActor->FindComponentByClass<USphereReflectionCaptureComponent>())
	{
		Comp->Modify();
		Comp->InfluenceRadius = InfluenceRadius;
		Comp->MarkRenderStateDirty();
	}

	return ApplyActorLabel(NewActor, Name);
}

bool ULightingService::UpdateReflectionCaptureContents()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("UpdateReflectionCaptureContents: no editor world available"));
		return false;
	}

	UReflectionCaptureComponent::UpdateReflectionCaptureContents(World);
	return true;
}

// =================================================================
// Lightmass
// =================================================================

FString ULightingService::SpawnLightmassImportanceVolume(
	const FString& Name,
	FVector Location,
	FVector Extent)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnLightmassImportanceVolume: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(FText::FromString(TEXT("Spawn Lightmass Importance Volume")));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Default volume brush is ±100 units. Convert requested extent to actor scale.
	const FVector SafeExtent(
		FMath::Max(Extent.X, 1.0f),
		FMath::Max(Extent.Y, 1.0f),
		FMath::Max(Extent.Z, 1.0f));

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetScale3D(SafeExtent / 100.0f);

	ALightmassImportanceVolume* NewActor = World->SpawnActor<ALightmassImportanceVolume>(
		ALightmassImportanceVolume::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyLighting, Warning, TEXT("SpawnLightmassImportanceVolume: SpawnActor returned null"));
		return FString();
	}

	NewActor->Modify();
	return ApplyActorLabel(NewActor, Name);
}
