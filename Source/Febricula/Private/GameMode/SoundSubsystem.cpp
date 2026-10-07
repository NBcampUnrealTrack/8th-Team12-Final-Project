// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/SoundSubsystem.h"

#include "Activity/ActivitySetting.h"
#include "Components/AudioComponent.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"

void USoundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const auto* S = GetDefault<UActivitySetting>();
	SoundTable = S->SoundTable.LoadSynchronous();
}

void USoundSubsystem::PlayBGM(USoundBase* NewBGM, float FadeTime)
{
	if (BGMChannel)
	{
		BGMChannel->FadeOut(FadeTime, .0f);
		BGMChannel = nullptr;
	}
	BGMChannel = UGameplayStatics::SpawnSound2D(this, NewBGM);
	BGMChannel->FadeIn(FadeTime, 1.f);
	
}

void USoundSubsystem::StopBGM(float FadeTime)
{
	if (!BGMChannel)
	{
		UE_LOG(LogTemp, Warning, TEXT("재생 중인 BGM 없음"));
		return;
	}
	BGMChannel->FadeOut(FadeTime, .0f);
	BGMChannel = nullptr;
}

void USoundSubsystem::PlaySE(FName ChannelName, USoundBase* NewSE, float FadeTime)
{
	UAudioComponent* NewComp = UGameplayStatics::SpawnSound2D(this, NewSE);
	if (!NewComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 오디오 생성 실패"), *ChannelName.ToString());
		return;
	}
	if (UAudioComponent* Old = SEChannels.FindRef(ChannelName))
	{
		Old->FadeOut(FadeTime, .0f);
	}
	SEChannels.Remove(ChannelName);
	NewComp->FadeIn(FadeTime, 1.f);
	SEChannels.Add(ChannelName, NewComp);
}

void USoundSubsystem::PlaySE3d(FName ChannelName, USoundBase* NewSE, FVector Location, float FadeTime)
{
	UAudioComponent* NewComp = UGameplayStatics::SpawnSoundAtLocation(this, NewSE, Location);
	if (!NewComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 오디오 생성 실패"), *ChannelName.ToString());
		return;
	}
	if (UAudioComponent* Old = SEChannels.FindRef(ChannelName))
	{
		Old->FadeOut(FadeTime, .0f);
	}
	SEChannels.Remove(ChannelName);
	NewComp->FadeIn(FadeTime, 1.f);
	SEChannels.Add(ChannelName, NewComp);
}

void USoundSubsystem::StopSE(FName ChannelName, float FadeTime)
{
	if (UAudioComponent* NowChannel = SEChannels.FindRef(ChannelName))
	{
		NowChannel->FadeOut(FadeTime, .0f);
		SEChannels.Remove(ChannelName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 존재하지 않는 채널"), *ChannelName.ToString());
	}
}

void USoundSubsystem::SetBGMVolume(float Volume)
{
}

void USoundSubsystem::SetSEVolume(FName ChannelTag, float Volume)
{
	if (UAudioComponent* NowChannel = *SEChannels.Find(ChannelTag))
	{
		NowChannel->SetVolumeMultiplier(Volume);
	}
}

USoundBase* USoundSubsystem::FoundSoundBase(FName SoundName)
{
	if (!SoundTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("사운드 테이블이 존재하지 않음"));
		return nullptr;
	}
	FSoundTableRow* Row = SoundTable->FindRow<FSoundTableRow>(SoundName, TEXT("FindSound"));
	if (!Row)
	{
	    UE_LOG(LogTemp, Warning, TEXT("%s: 사운드를 찾지 못함"), *SoundName.ToString());
		return nullptr;
	}
	return Row->Sound.LoadSynchronous();
}

void USoundSubsystem::RegisterSoundLoc(FName Tag, FVector Location)
{
	SoundLocations.Add(Tag, Location);	
}

void USoundSubsystem::UnregisterSoundLoc(FName Tag)
{
	if (!SoundLocations.Contains(Tag)) return;
	SoundLocations.Remove(Tag);
}
