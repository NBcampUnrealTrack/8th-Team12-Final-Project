// Fill out your copyright notice in the Description page of Project Settings.

#include "Settings/FebriculaSettingsSubsystem.h"

#include "Settings/SettingsSaveGame.h"
#include "GameMode/SoundSubsystem.h"
#include "Kismet/GameplayStatics.h"

void UFebriculaSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// SoundSubsystem이 먼저 초기화되어야 ApplyVolumeToSound가 정상 동작한다.
	Collection.InitializeDependency<USoundSubsystem>();

	LoadSettings();
}

// ---------------------------------------------------------------------------
// 볼륨
// ---------------------------------------------------------------------------

void UFebriculaSettingsSubsystem::SetBGMVolume(float Volume)
{
	BGMVolume = FMath::Clamp(Volume, 0.f, 1.f);
	ApplyVolumeToSound();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UFebriculaSettingsSubsystem::SetSFXVolume(float Volume)
{
	SFXVolume = FMath::Clamp(Volume, 0.f, 1.f);
	ApplyVolumeToSound();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UFebriculaSettingsSubsystem::SetMuted(bool bInMuted)
{
	bMuted = bInMuted;
	ApplyVolumeToSound();
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// 텍스트 속도
// ---------------------------------------------------------------------------

void UFebriculaSettingsSubsystem::SetTextSpeed(float Speed)
{
	TextSpeed = FMath::Clamp(Speed, 0.f, 1.f);
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

void UFebriculaSettingsSubsystem::SetAutoSpeed(float Speed)
{
	AutoSpeed = FMath::Clamp(Speed, 0.f, 1.f);
	SaveSettings();
	OnSettingsChanged.Broadcast();
}

float UFebriculaSettingsSubsystem::GetCharsPerSecond() const
{
	// 0 → 10글자/초(느림),  1 → 80글자/초(빠름)
	return FMath::Lerp(10.f, 80.f, FMath::Clamp(TextSpeed, 0.f, 1.f));
}

float UFebriculaSettingsSubsystem::GetAutoDelaySeconds() const
{
	// 0 → 3.0초 대기(느림),  1 → 0.5초 대기(빠름)
	return FMath::Lerp(3.0f, 0.5f, FMath::Clamp(AutoSpeed, 0.f, 1.f));
}

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

void UFebriculaSettingsSubsystem::ApplyVolumeToSound()
{
	USoundSubsystem* Sound = GetGameInstance()
		? GetGameInstance()->GetSubsystem<USoundSubsystem>()
		: nullptr;
	if (!Sound)
	{
		return;
	}

	const float AppliedBGM = bMuted ? 0.f : BGMVolume;
	const float AppliedSFX = bMuted ? 0.f : SFXVolume;
	Sound->SetBGMVolume(AppliedBGM);
	Sound->SetMasterSEVolume(AppliedSFX);
}

void UFebriculaSettingsSubsystem::SaveSettings()
{
	USettingsSaveGame* Save = Cast<USettingsSaveGame>(
		UGameplayStatics::CreateSaveGameObject(USettingsSaveGame::StaticClass()));
	if (!Save)
	{
		return;
	}
	Save->BGMVolume = BGMVolume;
	Save->SFXVolume = SFXVolume;
	Save->bMuted    = bMuted;
	Save->TextSpeed = TextSpeed;
	Save->AutoSpeed = AutoSpeed;

	UGameplayStatics::SaveGameToSlot(Save, SettingsSlotName, SettingsUserIndex);
}

void UFebriculaSettingsSubsystem::LoadSettings()
{
	if (!UGameplayStatics::DoesSaveGameExist(SettingsSlotName, SettingsUserIndex))
	{
		// 세이브 없으면 기본값 그대로 적용
		ApplyVolumeToSound();
		return;
	}

	USettingsSaveGame* Save = Cast<USettingsSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SettingsSlotName, SettingsUserIndex));
	if (!Save)
	{
		ApplyVolumeToSound();
		return;
	}

	BGMVolume = Save->BGMVolume;
	SFXVolume = Save->SFXVolume;
	bMuted    = Save->bMuted;
	TextSpeed = Save->TextSpeed;
	AutoSpeed = Save->AutoSpeed;

	ApplyVolumeToSound();
}
