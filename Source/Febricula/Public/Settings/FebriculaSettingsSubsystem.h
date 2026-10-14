// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FebriculaSettingsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsChanged);

/**
 * 게임 설정(볼륨/음소거/텍스트 속도)을 관리하는 GameInstanceSubsystem.
 *
 * - 값이 바뀔 때마다 "Settings" 슬롯에 자동 저장한다.
 * - Initialize에서 저장된 값을 불러와 USoundSubsystem에 즉시 적용한다.
 *   (USoundSubsystem 의존성을 직접 선언해서 초기화 순서 문제를 방지)
 * - 음소거 중에는 볼륨 값 자체는 보존하고, SoundSubsystem에만 0을 넘긴다.
 */
UCLASS()
class FEBRICULA_API UFebriculaSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- 볼륨 ----

	UFUNCTION(BlueprintCallable, Category = "Settings | Sound")
	void SetBGMVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Settings | Sound")
	float GetBGMVolume() const { return BGMVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings | Sound")
	void SetSFXVolume(float Volume);

	UFUNCTION(BlueprintPure, Category = "Settings | Sound")
	float GetSFXVolume() const { return SFXVolume; }

	UFUNCTION(BlueprintCallable, Category = "Settings | Sound")
	void SetMuted(bool bInMuted);

	UFUNCTION(BlueprintPure, Category = "Settings | Sound")
	bool GetMuted() const { return bMuted; }

	// ---- 텍스트 속도 ----

	UFUNCTION(BlueprintCallable, Category = "Settings | Text")
	void SetTextSpeed(float Speed);

	UFUNCTION(BlueprintPure, Category = "Settings | Text")
	float GetTextSpeed() const { return TextSpeed; }

	UFUNCTION(BlueprintCallable, Category = "Settings | Text")
	void SetAutoSpeed(float Speed);

	UFUNCTION(BlueprintPure, Category = "Settings | Text")
	float GetAutoSpeed() const { return AutoSpeed; }

	/**
	 * TextSpeed(0~1) → 초당 글자 수(10~80).
	 * 대화 위젯이 타이핑 효과 속도를 계산할 때 사용한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Settings | Text")
	float GetCharsPerSecond() const;

	/**
	 * AutoSpeed(0~1) → 자동 진행 대기 시간(3.0~0.5초).
	 * 값이 클수록(빠를수록) 딜레이가 짧아진다.
	 */
	UFUNCTION(BlueprintPure, Category = "Settings | Text")
	float GetAutoDelaySeconds() const;

	// ---- 델리게이트 ----

	/** 어떤 설정값이든 변경될 때마다 발생. BP에서 설정 UI 갱신에 사용 */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsChanged OnSettingsChanged;

private:
	void ApplyVolumeToSound();
	void SaveSettings();
	void LoadSettings();

	UPROPERTY() float BGMVolume = 0.8f;
	UPROPERTY() float SFXVolume = 0.8f;
	UPROPERTY() bool  bMuted    = false;
	UPROPERTY() float TextSpeed = 0.5f;
	UPROPERTY() float AutoSpeed = 0.5f;

	static constexpr const TCHAR* SettingsSlotName = TEXT("Settings");
	static constexpr int32 SettingsUserIndex = 0;
};
