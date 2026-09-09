// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Transform.h"
#include "USequencerService.generated.h"

/**
 * Common result envelope returned by mutating Sequencer operations that
 * want to convey both an identifier / info payload and a diagnostic reason
 * on failure. Individual UFUNCTIONs prefer FString / bool for Python
 * ergonomics, but this struct is available for callers that need both.
 *
 * @note Mirrors the FStateTreeCompileResult / FStateTreeTaskPropertySetResult
 *       shape used elsewhere in Revvy.
 */
USTRUCT(BlueprintType)
struct FSequencerResult
{
	GENERATED_BODY()

	/** Whether the operation succeeded */
	UPROPERTY(BlueprintReadWrite, Category = "Sequencer")
	bool bSuccess = false;

	/** Informational payload (asset path, binding GUID, section GUID, etc.) */
	UPROPERTY(BlueprintReadWrite, Category = "Sequencer")
	FString Info;

	/** Human-readable error message when bSuccess is false */
	UPROPERTY(BlueprintReadWrite, Category = "Sequencer")
	FString ErrorMessage;
};

/**
 * Cinematic authoring service exposed directly to Python for creating and
 * editing ULevelSequence assets, adding bindings (possessables / spawnables),
 * scheduling tracks and sections, keying channels, spawning cinematic camera
 * rigs and enqueueing Movie Render Queue 2 (MRQ2) jobs.
 *
 * Mirrors the UActorService / ULightingService pattern: all UFUNCTIONs are
 * static and BlueprintCallable so they are reachable from Python as
 * `unreal.SequencerService.<snake_case_name>(...)`.
 *
 * UE 5.8 API notes:
 *  - `UMovieScene::AddTrack<T>()` supersedes the deprecated `AddMasterTrack<T>()`.
 *  - Transform sections use `FMovieSceneDoubleChannel` (UE 5.0+); float property
 *    tracks still use `FMovieSceneFloatChannel`; bool property tracks use
 *    `FMovieSceneBoolChannel`.
 *  - Section identifiers are exposed as `UMovieSceneSignedObject::GetSignature()`
 *    stringified with `EGuidFormats::Digits`.
 *  - Frame numbers are interpreted in the owning MovieScene's TickResolution
 *    (default 60000 fps). Convert seconds via `TickResolution.AsFrameNumber(seconds)`
 *    on the Python side or use display-rate helpers.
 *  - Rendering uses the Movie Render Queue 2 subsystem
 *    (`UMoviePipelineQueueSubsystem`) which supersedes the legacy
 *    `UAutomatedLevelSequenceCapture` matinee capture path.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.SequencerService
 *
 *   # Lifecycle
 *   seq_path = svc.create_level_sequence("/Game/Cinematics/Intro_LS", 5.0, 30.0)
 *   svc.save_level_sequence(seq_path)
 *
 *   # Bindings
 *   cam_guid = svc.spawn_cine_camera_actor("CineCam_Intro",
 *                                          unreal.Vector(0, -500, 200),
 *                                          unreal.Rotator(-10, 0, 0))
 *   binding = svc.add_possessable_actor(seq_path, "CineCam_Intro")
 *
 *   # Tracks + Sections
 *   xform_sig = svc.add_transform_track(seq_path, binding)
 *   svc.set_section_range(seq_path, xform_sig, 0, 150000)  # 0..2.5s @ 60000
 *   svc.add_key_transform(seq_path, xform_sig, 0,
 *                          unreal.Transform(unreal.Vector(0, -500, 200),
 *                                           unreal.Rotator(0, 0, 0),
 *                                           unreal.Vector(1, 1, 1)))
 *
 *   # Camera Cut Track
 *   cut_sig  = svc.add_camera_cut_track(seq_path)
 *   svc.add_camera_cut(seq_path, binding, 0, 150000)
 *
 *   # Render via MRQ2
 *   svc.enqueue_mrq_render(seq_path, "/Game/MoviePipeline/DefaultConfig")
 *
 * @note All 17 sequencer operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API USequencerService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new empty ULevelSequence asset at the given content path.
	 * Configures MovieScene TickResolution (24000 fps) + DisplayRate (frame rate)
	 * and sets the initial playback range to [0, DurationSeconds].
	 *
	 * @param AssetPath        - Content path for the new asset, e.g. "/Game/Cinematics/Intro_LS"
	 * @param DurationSeconds  - Initial playback duration in seconds
	 * @param FrameRate        - Editor display rate in FPS (default 30 fps)
	 * @return Content path of the created asset, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Lifecycle")
	static FString CreateLevelSequence(
		const FString& AssetPath,
		float DurationSeconds,
		float FrameRate = 30.0f);

	/**
	 * Save an existing ULevelSequence asset to disk.
	 *
	 * @param AssetPath - Content path of the sequence asset
	 * @return True if the save succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Lifecycle")
	static bool SaveLevelSequence(const FString& AssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Binding
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Add a possessable binding for an existing actor in the current editor world.
	 * Prefers ULevelSequenceEditorSubsystem::AddActors when GEditor is available.
	 *
	 * @param SequencePath      - Content path of the ULevelSequence asset
	 * @param ActorNameOrPath   - Actor label, name, or soft object path
	 * @return Binding FGuid (EGuidFormats::Digits) or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Binding")
	static FString AddPossessableActor(
		const FString& SequencePath,
		const FString& ActorNameOrPath);

	/**
	 * Add a spawnable binding for an actor class template. The template class
	 * is instantiated as a CDO snapshot and stored inside the MovieScene.
	 *
	 * @param SequencePath          - Content path of the ULevelSequence asset
	 * @param TemplateActorClassPath - Class path, e.g. "/Script/Engine.PointLight"
	 * @param Name                   - Display name for the spawnable
	 * @return Binding FGuid (EGuidFormats::Digits) or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Binding")
	static FString AddSpawnable(
		const FString& SequencePath,
		const FString& TemplateActorClassPath,
		const FString& Name);

	/**
	 * Remove a possessable or spawnable binding (and all its tracks) from the sequence.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param BindingGuidStr  - Binding FGuid as string (any FGuid::Parse-compatible format)
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Binding")
	static bool RemoveBinding(
		const FString& SequencePath,
		const FString& BindingGuidStr);

	// ═══════════════════════════════════════════════════════════════════
	// Tracks
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Add a 3D transform track (with an initial section spanning the sequence
	 * playback range) to the given binding.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param BindingGuidStr  - Binding FGuid as string
	 * @return Section signature GUID (EGuidFormats::Digits) or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Tracks")
	static FString AddTransformTrack(
		const FString& SequencePath,
		const FString& BindingGuidStr);

	/**
	 * Add a float property track (with an initial section) for the given binding.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param BindingGuidStr  - Binding FGuid as string
	 * @param PropertyPath    - Dotted property path, e.g. "LightComponent.Intensity"
	 * @return Section signature GUID or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Tracks")
	static FString AddFloatPropertyTrack(
		const FString& SequencePath,
		const FString& BindingGuidStr,
		const FString& PropertyPath);

	/**
	 * Add a bool property track (with an initial section) for the given binding.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param BindingGuidStr  - Binding FGuid as string
	 * @param PropertyPath    - Dotted property path, e.g. "bHidden"
	 * @return Section signature GUID or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Tracks")
	static FString AddBoolPropertyTrack(
		const FString& SequencePath,
		const FString& BindingGuidStr,
		const FString& PropertyPath);

	/**
	 * Add (or reuse) the camera cut track on the sequence. There is only ever
	 * one camera cut track per sequence.
	 *
	 * @param SequencePath - Content path of the ULevelSequence asset
	 * @return Track signature GUID or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Tracks")
	static FString AddCameraCutTrack(const FString& SequencePath);

	// ═══════════════════════════════════════════════════════════════════
	// Sections + Keys
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the frame range on an existing section identified by its signature GUID.
	 * Frame numbers are in the MovieScene's TickResolution (default 60000 fps).
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param SectionGuidStr  - Section signature GUID as string
	 * @param StartFrame      - Start frame (tick-resolution frame number)
	 * @param EndFrame        - End frame (tick-resolution frame number, exclusive)
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Sections")
	static bool SetSectionRange(
		const FString& SequencePath,
		const FString& SectionGuidStr,
		int32 StartFrame,
		int32 EndFrame);

	/**
	 * Add a keyframe on a float channel of the given section.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param SectionGuidStr  - Section signature GUID as string
	 * @param ChannelIndex    - Zero-based index into the section's float channel proxy
	 * @param FrameNumber     - Tick-resolution frame number for the key
	 * @param Value           - Float value at the key
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Keys")
	static bool AddKeyFloat(
		const FString& SequencePath,
		const FString& SectionGuidStr,
		int32 ChannelIndex,
		int32 FrameNumber,
		float Value);

	/**
	 * Add nine keys (Location XYZ, Rotation Roll/Pitch/Yaw, Scale XYZ) to a
	 * transform section using its FMovieSceneDoubleChannel proxy.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param SectionGuidStr  - Transform section signature GUID as string
	 * @param FrameNumber     - Tick-resolution frame number for the key
	 * @param Value           - Full transform to key
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Keys")
	static bool AddKeyTransform(
		const FString& SequencePath,
		const FString& SectionGuidStr,
		int32 FrameNumber,
		FTransform Value);

	/**
	 * Add a keyframe on a bool channel of the given section.
	 *
	 * @param SequencePath    - Content path of the ULevelSequence asset
	 * @param SectionGuidStr  - Section signature GUID as string
	 * @param ChannelIndex    - Zero-based index into the section's bool channel proxy
	 * @param FrameNumber     - Tick-resolution frame number for the key
	 * @param Value           - Bool value at the key
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Keys")
	static bool AddKeyBool(
		const FString& SequencePath,
		const FString& SectionGuidStr,
		int32 ChannelIndex,
		int32 FrameNumber,
		bool Value);

	/**
	 * Add a new camera cut section on the sequence's camera cut track.
	 *
	 * @param SequencePath          - Content path of the ULevelSequence asset
	 * @param CameraBindingGuidStr  - Binding GUID of the camera possessable/spawnable
	 * @param StartFrame            - Tick-resolution start frame
	 * @param EndFrame              - Tick-resolution end frame
	 * @return New camera-cut section signature GUID or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Keys")
	static FString AddCameraCut(
		const FString& SequencePath,
		const FString& CameraBindingGuidStr,
		int32 StartFrame,
		int32 EndFrame);

	// ═══════════════════════════════════════════════════════════════════
	// Camera Rigs (CinematicCamera module)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn an ACineCameraActor in the current editor world.
	 *
	 * @param Name     - Display label to assign to the new actor
	 * @param Location - World-space spawn location
	 * @param Rotation - World-space rotation
	 * @return Actor label of the spawned camera, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Camera")
	static FString SpawnCineCameraActor(
		const FString& Name,
		FVector Location,
		FRotator Rotation);

	/**
	 * Spawn an ACameraRig_Rail in the current editor world and populate its
	 * USplineComponent with the provided spline points (world-space).
	 *
	 * @param Name         - Display label to assign to the new actor
	 * @param SplinePoints - World-space points to build the rail spline from
	 * @return Actor label of the spawned rail rig, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Camera")
	static FString SpawnCameraRigRail(
		const FString& Name,
		const TArray<FVector>& SplinePoints);

	// ═══════════════════════════════════════════════════════════════════
	// Render (Movie Render Queue 2)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enqueue a ULevelSequence render job on the MRQ2 queue subsystem.
	 * Optionally applies a preset UMoviePipelinePrimaryConfig when a config
	 * asset path is supplied.
	 *
	 * @param SequencePath        - Content path of the ULevelSequence asset
	 * @param PipelineConfigPath  - Content path of a UMoviePipelinePrimaryConfig
	 *                              preset (may be empty to use queue defaults)
	 * @return True if the job was successfully allocated and queued
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Sequencer|Render")
	static bool EnqueueMRQRender(
		const FString& SequencePath,
		const FString& PipelineConfigPath);

private:
	/** Load the ULevelSequence at the given content path. */
	static class ULevelSequence* LoadLevelSequence(const FString& AssetPath);

	/**
	 * Locate a section inside the sequence's MovieScene by its Signature GUID.
	 * @return the section pointer or nullptr if not found.
	 */
	static class UMovieSceneSection* FindSectionBySignature(
		class ULevelSequence* Sequence,
		const FGuid& SignatureGuid);

	/** Apply an actor label if non-empty and return the final label. */
	static FString ApplyActorLabel(class AActor* Actor, const FString& DesiredName);
};
