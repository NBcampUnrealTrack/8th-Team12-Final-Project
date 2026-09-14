// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/USequencerService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvySequencer, Log, All);

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// Core / Editor
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// World / Actors
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// Level Sequence
#include "LevelSequence.h"
#include "LevelSequenceEditorSubsystem.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"

// MovieScene core
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneBindingProxy.h"
#include "MovieSceneObjectBindingID.h"

// Tracks + Sections
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieScenePropertyTrack.h"

// Channels
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"

// Frame rate
#include "Misc/FrameRate.h"
#include "Misc/FrameNumber.h"

// Cinematic camera
#include "CineCameraActor.h"
#include "CameraRig_Rail.h"
#include "Components/SplineComponent.h"

// Movie Render Queue 2
// NOTE: UMoviePipelineExecutorJob lives in MoviePipelineQueue.h; UMoviePipelinePIEExecutor
// (in MovieRenderPipelineEditor) is intentionally not included — we allocate a plain
// executor job class and let the queue pick the executor when it runs.
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelinePrimaryConfig.h"

// =================================================================
// Helpers
// =================================================================

ULevelSequence* USequencerService::LoadLevelSequence(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<ULevelSequence>(Loaded);
}

UMovieSceneSection* USequencerService::FindSectionBySignature(
	ULevelSequence* Sequence,
	const FGuid& SignatureGuid)
{
	if (!Sequence || !SignatureGuid.IsValid())
	{
		return nullptr;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return nullptr;
	}

	// All per-binding tracks (possessable / spawnable).
	// Const access: non-const GetBindings() is deprecated in 5.8.
	const UMovieScene* ConstMovieScene = MovieScene;
	for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (!Track) continue;
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (Section && Section->GetSignature() == SignatureGuid)
				{
					return Section;
				}
			}
		}
	}

	// Root / master tracks (camera cut, etc.)
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (!Track) continue;
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (Section && Section->GetSignature() == SignatureGuid)
			{
				return Section;
			}
		}
	}

	// Camera cut track is stored separately in some UE versions
	if (UMovieSceneTrack* CamCut = MovieScene->GetCameraCutTrack())
	{
		for (UMovieSceneSection* Section : CamCut->GetAllSections())
		{
			if (Section && Section->GetSignature() == SignatureGuid)
			{
				return Section;
			}
		}
	}

	return nullptr;
}

FString USequencerService::ApplyActorLabel(AActor* Actor, const FString& DesiredName)
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
// Lifecycle
// =================================================================

FString USequencerService::CreateLevelSequence(
	const FString& AssetPath,
	float DurationSeconds,
	float FrameRate)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("CreateLevelSequence: empty AssetPath"));
		return FString();
	}

	// Split "/Game/Foo/Bar" into ("/Game/Foo", "Bar")
	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName   = FPackageName::GetShortName(AssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("CreateLevelSequence: invalid AssetPath '%s'"), *AssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "CreateLevelSequence", "Create Level Sequence"));

	// ULevelSequenceFactoryNew lives in the editor-private module
	// (LevelSequenceEditor/Private/Factories/) and is not linkable in UE 5.8,
	// so create the package + asset directly instead of via AssetTools.CreateAsset.
	const FString FullName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullName);
	if (!Package)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("CreateLevelSequence: CreatePackage failed for '%s'"), *AssetPath);
		return FString();
	}

	ULevelSequence* Sequence = NewObject<ULevelSequence>(
		Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Sequence)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("CreateLevelSequence: NewObject<ULevelSequence> failed for '%s'"), *AssetPath);
		return FString();
	}

	Sequence->Initialize();

	FAssetRegistryModule::AssetCreated(Sequence);
	Package->MarkPackageDirty();

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("CreateLevelSequence: sequence has no MovieScene ('%s')"), *AssetPath);
		return FString();
	}

	MovieScene->Modify();

	// Standard timecode-friendly resolution used by MRQ / Sequencer defaults.
	const FFrameRate TickResolution(24000, 1);
	const FFrameRate DisplayRate(
		FMath::Max(1, FMath::RoundToInt(FrameRate)), 1);

	MovieScene->SetTickResolutionDirectly(TickResolution);
	MovieScene->SetDisplayRate(DisplayRate);

	const float SafeDuration = FMath::Max(0.0f, DurationSeconds);
	const FFrameNumber Start = FFrameNumber(0);
	const FFrameNumber End   = TickResolution.AsFrameNumber(SafeDuration);
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));

	return Sequence->GetPathName();
}

bool USequencerService::SaveLevelSequence(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("SaveLevelSequence: empty AssetPath"));
		return false;
	}
	return UEditorAssetLibrary::SaveAsset(AssetPath, false);
}

// =================================================================
// Binding
// =================================================================

FString USequencerService::AddPossessableActor(
	const FString& SequencePath,
	const FString& ActorNameOrPath)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddPossessableActor: failed to load sequence '%s'"), *SequencePath);
		return FString();
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FString();
	}

	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddPossessableActor: actor '%s' not found in editor world"), *ActorNameOrPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddPossessable", "Add Possessable Actor"));
	Sequence->Modify();
	MovieScene->Modify();

	// Preferred editor path — handles world context + binding reference automatically.
	if (GEditor)
	{
		if (ULevelSequenceEditorSubsystem* LSES = GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>())
		{
			TArray<AActor*> Actors = { Actor };
			TArray<FMovieSceneBindingProxy> Proxies = LSES->AddActors(Actors);
			if (Proxies.Num() > 0 && Proxies[0].BindingID.IsValid())
			{
				return Proxies[0].BindingID.ToString(EGuidFormats::Digits);
			}
		}
	}

	// Fallback low-level path
	const FGuid BindingGuid = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
	if (!BindingGuid.IsValid())
	{
		return FString();
	}
	Sequence->BindPossessableObject(BindingGuid, *Actor, Actor->GetWorld());
	return BindingGuid.ToString(EGuidFormats::Digits);
}

FString USequencerService::AddSpawnable(
	const FString& SequencePath,
	const FString& TemplateActorClassPath,
	const FString& Name)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddSpawnable: failed to load sequence '%s'"), *SequencePath);
		return FString();
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FString();
	}

	UClass* TemplateClass = LoadObject<UClass>(nullptr, *TemplateActorClassPath);
	if (!TemplateClass || !TemplateClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddSpawnable: '%s' is not a valid AActor-derived class"), *TemplateActorClassPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddSpawnable", "Add Spawnable"));
	Sequence->Modify();
	MovieScene->Modify();

	UObject* Template = NewObject<UObject>(MovieScene, TemplateClass, NAME_None, RF_Transactional);
	if (!Template)
	{
		return FString();
	}

	const FString DesiredName = Name.IsEmpty() ? TemplateClass->GetName() : Name;
	const FGuid BindingGuid = MovieScene->AddSpawnable(DesiredName, *Template);
	if (!BindingGuid.IsValid())
	{
		return FString();
	}
	return BindingGuid.ToString(EGuidFormats::Digits);
}

bool USequencerService::RemoveBinding(
	const FString& SequencePath,
	const FString& BindingGuidStr)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	FGuid BindingGuid;
	if (!FGuid::Parse(BindingGuidStr, BindingGuid))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("RemoveBinding: invalid GUID string '%s'"), *BindingGuidStr);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "RemoveBinding", "Remove Binding"));
	Sequence->Modify();
	MovieScene->Modify();

	if (MovieScene->FindPossessable(BindingGuid))
	{
		if (MovieScene->RemovePossessable(BindingGuid))
		{
			Sequence->UnbindPossessableObjects(BindingGuid);
			return true;
		}
	}
	if (MovieScene->FindSpawnable(BindingGuid))
	{
		return MovieScene->RemoveSpawnable(BindingGuid);
	}

	UE_LOG(LogRevvySequencer, Warning,
		TEXT("RemoveBinding: GUID '%s' not found in sequence '%s'"),
		*BindingGuidStr, *SequencePath);
	return false;
}

// =================================================================
// Tracks
// =================================================================

FString USequencerService::AddTransformTrack(
	const FString& SequencePath,
	const FString& BindingGuidStr)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return FString();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return FString();

	FGuid BindingGuid;
	if (!FGuid::Parse(BindingGuidStr, BindingGuid))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddTransformTrack: invalid GUID '%s'"), *BindingGuidStr);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddTransformTrack", "Add Transform Track"));
	Sequence->Modify();
	MovieScene->Modify();

	UMovieScene3DTransformTrack* Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
	if (!Track)
	{
		return FString();
	}
	Track->Modify();

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (!Section)
	{
		return FString();
	}
	Section->SetRange(MovieScene->GetPlaybackRange());
	Track->AddSection(*Section);

	return Section->GetSignature().ToString(EGuidFormats::Digits);
}

namespace RevvySequencerServiceLocal
{
static FString AddPropertyTrackByClass(
	ULevelSequence* Sequence,
	const FString& BindingGuidStr,
	const FString& PropertyPath,
	UClass* TrackClass,
	const TCHAR* TxKey,
	const TCHAR* TxLabel)
{
	if (!Sequence || !TrackClass) return FString();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return FString();

	FGuid BindingGuid;
	if (!FGuid::Parse(BindingGuidStr, BindingGuid))
	{
		return FString();
	}

	FScopedTransaction Tx(FText::AsCultureInvariant(FString(TxLabel)));
	Sequence->Modify();
	MovieScene->Modify();

	UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
	if (!NewTrack) return FString();
	NewTrack->Modify();

	if (UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(NewTrack))
	{
		// Take the leaf identifier after the last '.' as the property name
		// (e.g. "LightComponent.Intensity" -> "Intensity").
		FString LeafName;
		if (!PropertyPath.Split(TEXT("."), nullptr, &LeafName,
			ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			LeafName = PropertyPath;
		}
		PropTrack->SetPropertyNameAndPath(FName(*LeafName), PropertyPath);
	}

	UMovieSceneSection* Section = NewTrack->CreateNewSection();
	if (!Section) return FString();
	Section->SetRange(MovieScene->GetPlaybackRange());
	NewTrack->AddSection(*Section);

	return Section->GetSignature().ToString(EGuidFormats::Digits);
}
} // namespace RevvySequencerServiceLocal

FString USequencerService::AddFloatPropertyTrack(
	const FString& SequencePath,
	const FString& BindingGuidStr,
	const FString& PropertyPath)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddFloatPropertyTrack: failed to load '%s'"), *SequencePath);
		return FString();
	}
	return RevvySequencerServiceLocal::AddPropertyTrackByClass(
		Sequence, BindingGuidStr, PropertyPath,
		UMovieSceneFloatTrack::StaticClass(),
		TEXT("AddFloatPropertyTrack"),
		TEXT("Add Float Property Track"));
}

FString USequencerService::AddBoolPropertyTrack(
	const FString& SequencePath,
	const FString& BindingGuidStr,
	const FString& PropertyPath)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddBoolPropertyTrack: failed to load '%s'"), *SequencePath);
		return FString();
	}
	return RevvySequencerServiceLocal::AddPropertyTrackByClass(
		Sequence, BindingGuidStr, PropertyPath,
		UMovieSceneBoolTrack::StaticClass(),
		TEXT("AddBoolPropertyTrack"),
		TEXT("Add Bool Property Track"));
}

FString USequencerService::AddCameraCutTrack(const FString& SequencePath)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return FString();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return FString();

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddCameraCutTrack", "Add Camera Cut Track"));
	Sequence->Modify();
	MovieScene->Modify();

	UMovieSceneCameraCutTrack* Track =
		Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (!Track)
	{
		Track = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	}
	if (!Track)
	{
		return FString();
	}
	return Track->GetSignature().ToString(EGuidFormats::Digits);
}

// =================================================================
// Sections + Keys
// =================================================================

bool USequencerService::SetSectionRange(
	const FString& SequencePath,
	const FString& SectionGuidStr,
	int32 StartFrame,
	int32 EndFrame)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return false;

	FGuid SectionGuid;
	if (!FGuid::Parse(SectionGuidStr, SectionGuid))
	{
		return false;
	}

	UMovieSceneSection* Section = FindSectionBySignature(Sequence, SectionGuid);
	if (!Section)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("SetSectionRange: section '%s' not found"), *SectionGuidStr);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "SetSectionRange", "Set Section Range"));
	Sequence->GetMovieScene()->Modify();
	Section->Modify();
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(EndFrame)));
	return true;
}

bool USequencerService::AddKeyFloat(
	const FString& SequencePath,
	const FString& SectionGuidStr,
	int32 ChannelIndex,
	int32 FrameNumber,
	float Value)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return false;

	FGuid SectionGuid;
	if (!FGuid::Parse(SectionGuidStr, SectionGuid))
	{
		return false;
	}

	UMovieSceneSection* Section = FindSectionBySignature(Sequence, SectionGuid);
	if (!Section) return false;

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();
	if (!Channels.IsValidIndex(ChannelIndex))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddKeyFloat: channel index %d out of range (have %d)"),
			ChannelIndex, Channels.Num());
		return false;
	}
	FMovieSceneFloatChannel* Channel = Channels[ChannelIndex];
	if (!Channel) return false;

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddKeyFloat", "Add Float Key"));
	Section->Modify();
	Channel->GetData().AddKey(FFrameNumber(FrameNumber), FMovieSceneFloatValue(Value));
	return true;
}

bool USequencerService::AddKeyTransform(
	const FString& SequencePath,
	const FString& SectionGuidStr,
	int32 FrameNumber,
	FTransform Value)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return false;

	FGuid SectionGuid;
	if (!FGuid::Parse(SectionGuidStr, SectionGuid))
	{
		return false;
	}

	UMovieSceneSection* Section = FindSectionBySignature(Sequence, SectionGuid);
	if (!Section) return false;

	// Transform sections use FMovieSceneDoubleChannel in UE 5.x.
	// Channel layout: 0..2 Location XYZ, 3..5 Rotation Roll/Pitch/Yaw, 6..8 Scale XYZ.
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneDoubleChannel*> Doubles = Proxy.GetChannels<FMovieSceneDoubleChannel>();
	if (Doubles.Num() < 9)
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddKeyTransform: expected >=9 double channels, got %d"), Doubles.Num());
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddKeyTransform", "Add Transform Key"));
	Section->Modify();

	const FFrameNumber Time(FrameNumber);
	const FVector  Loc = Value.GetLocation();
	const FRotator Rot = Value.Rotator();
	const FVector  Scl = Value.GetScale3D();

	Doubles[0]->AddCubicKey(Time, Loc.X);
	Doubles[1]->AddCubicKey(Time, Loc.Y);
	Doubles[2]->AddCubicKey(Time, Loc.Z);
	Doubles[3]->AddCubicKey(Time, Rot.Roll);
	Doubles[4]->AddCubicKey(Time, Rot.Pitch);
	Doubles[5]->AddCubicKey(Time, Rot.Yaw);
	Doubles[6]->AddCubicKey(Time, Scl.X);
	Doubles[7]->AddCubicKey(Time, Scl.Y);
	Doubles[8]->AddCubicKey(Time, Scl.Z);
	return true;
}

bool USequencerService::AddKeyBool(
	const FString& SequencePath,
	const FString& SectionGuidStr,
	int32 ChannelIndex,
	int32 FrameNumber,
	bool Value)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return false;

	FGuid SectionGuid;
	if (!FGuid::Parse(SectionGuidStr, SectionGuid))
	{
		return false;
	}

	UMovieSceneSection* Section = FindSectionBySignature(Sequence, SectionGuid);
	if (!Section) return false;

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneBoolChannel*> Channels = Proxy.GetChannels<FMovieSceneBoolChannel>();
	if (!Channels.IsValidIndex(ChannelIndex))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddKeyBool: channel index %d out of range (have %d)"),
			ChannelIndex, Channels.Num());
		return false;
	}
	FMovieSceneBoolChannel* Channel = Channels[ChannelIndex];
	if (!Channel) return false;

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddKeyBool", "Add Bool Key"));
	Section->Modify();
	Channel->GetData().AddKey(FFrameNumber(FrameNumber), Value);
	return true;
}

FString USequencerService::AddCameraCut(
	const FString& SequencePath,
	const FString& CameraBindingGuidStr,
	int32 StartFrame,
	int32 EndFrame)
{
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence) return FString();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return FString();

	FGuid CamBindingGuid;
	if (!FGuid::Parse(CameraBindingGuidStr, CamBindingGuid))
	{
		UE_LOG(LogRevvySequencer, Warning,
			TEXT("AddCameraCut: invalid GUID '%s'"), *CameraBindingGuidStr);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "AddCameraCut", "Add Camera Cut"));
	Sequence->Modify();
	MovieScene->Modify();

	UMovieSceneCameraCutTrack* Track =
		Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (!Track)
	{
		Track = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	}
	if (!Track)
	{
		return FString();
	}
	Track->Modify();

	const FMovieSceneObjectBindingID CameraBindingID(CamBindingGuid);
	UMovieSceneCameraCutSection* Section =
		Track->AddNewCameraCut(CameraBindingID, FFrameNumber(StartFrame));
	if (!Section)
	{
		return FString();
	}
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(EndFrame)));
	return Section->GetSignature().ToString(EGuidFormats::Digits);
}

// =================================================================
// Camera Rigs
// =================================================================

FString USequencerService::SpawnCineCameraActor(
	const FString& Name,
	FVector Location,
	FRotator Rotation)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("SpawnCineCameraActor: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "SpawnCineCamera", "Spawn Cine Camera Actor"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetRotation(Rotation.Quaternion());
	SpawnT.SetScale3D(FVector::OneVector);

	ACineCameraActor* NewActor = World->SpawnActor<ACineCameraActor>(
		ACineCameraActor::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("SpawnCineCameraActor: SpawnActor returned null"));
		return FString();
	}
	NewActor->Modify();
	return ApplyActorLabel(NewActor, Name);
}

FString USequencerService::SpawnCameraRigRail(
	const FString& Name,
	const TArray<FVector>& SplinePoints)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("SpawnCameraRigRail: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "SpawnCameraRigRail", "Spawn Camera Rig Rail"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector SpawnLocation = SplinePoints.Num() > 0 ? SplinePoints[0] : FVector::ZeroVector;

	FTransform SpawnT;
	SpawnT.SetLocation(SpawnLocation);

	ACameraRig_Rail* NewActor = World->SpawnActor<ACameraRig_Rail>(
		ACameraRig_Rail::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("SpawnCameraRigRail: SpawnActor returned null"));
		return FString();
	}
	NewActor->Modify();

	if (USplineComponent* Spline = NewActor->FindComponentByClass<USplineComponent>())
	{
		Spline->Modify();
		Spline->ClearSplinePoints(false);
		for (int32 Idx = 0; Idx < SplinePoints.Num(); ++Idx)
		{
			// SplinePoints incoming in world-space; convert to local relative to actor.
			const FVector LocalPoint = SplinePoints[Idx] - SpawnLocation;
			Spline->AddSplinePoint(LocalPoint, ESplineCoordinateSpace::Local, false);
		}
		Spline->UpdateSpline();
	}

	return ApplyActorLabel(NewActor, Name);
}

// =================================================================
// Render (Movie Render Queue 2)
// =================================================================

bool USequencerService::EnqueueMRQRender(
	const FString& SequencePath,
	const FString& PipelineConfigPath)
{
	if (!GEditor)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("EnqueueMRQRender: GEditor unavailable"));
		return false;
	}
	if (SequencePath.IsEmpty())
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("EnqueueMRQRender: empty SequencePath"));
		return false;
	}

	UMoviePipelineQueueSubsystem* QueueSubsystem =
		GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	if (!QueueSubsystem)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("EnqueueMRQRender: MoviePipelineQueueSubsystem missing"));
		return false;
	}

	UMoviePipelineQueue* Queue = QueueSubsystem->GetQueue();
	if (!Queue)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("EnqueueMRQRender: no active queue"));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("SequencerService", "EnqueueMRQRender", "Enqueue MRQ Render"));
	Queue->Modify();

	// AllocateNewJob takes the JOB class, not the executor class.
	UMoviePipelineExecutorJob* Job =
		Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	if (!Job)
	{
		UE_LOG(LogRevvySequencer, Warning, TEXT("EnqueueMRQRender: AllocateNewJob returned null"));
		return false;
	}
	Job->Modify();

	const FSoftObjectPath SeqSoft(SequencePath);
	Job->Sequence = SeqSoft;
	Job->JobName  = SeqSoft.GetAssetName();

	// Derive the map from the current editor world so the render has a world to run in.
	if (UWorld* EditorWorld = URevvyCommonHelpers::GetEditorWorldSafe())
	{
		Job->Map = FSoftObjectPath(EditorWorld);
	}

	if (!PipelineConfigPath.IsEmpty())
	{
		UMoviePipelinePrimaryConfig* Config =
			LoadObject<UMoviePipelinePrimaryConfig>(nullptr, *PipelineConfigPath);
		if (Config)
		{
			Job->SetConfiguration(Config);
		}
		else
		{
			UE_LOG(LogRevvySequencer, Warning,
				TEXT("EnqueueMRQRender: failed to load pipeline config '%s'"), *PipelineConfigPath);
		}
	}

	return true;
}
