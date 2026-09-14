// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Revvy : ModuleRules
{
	public Revvy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.KeepAsIs;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		// Unity build stays ON, and stays correct only because the helper names in
		// this module are unique per file.
		//
		// A unity build concatenates several .cpp files into one translation unit.
		// This module used to define its own file-local helpers under the same names
		// in dozens of files - JsonObjectToString in 20, SerializeJson in 16,
		// JsonArrayToString in 15 - and internal linkage stops protecting you the
		// moment two of those files land in one TU: it becomes a redefinition
		// (C2084), or an ambiguous call through ADL (C2668) where one candidate sits
		// in a named namespace and the other at file scope.
		//
		// Which files group together is decided per consuming project, not here.
		// A tester's project failed with 30+ such errors across
		// Module.Revvy.{1..11}.cpp while the identical source built fine locally,
		// because their adaptive-unity working set reshuffled the blobs
		// (2026-08-14, UE 5.8). A build that depends on that kind of luck is not a
		// build.
		//
		// 99 helpers across 46 files were renamed to be file-unique, so this is safe
		// again. If you add a file-local helper, give it a name no other .cpp in the
		// module uses - or put it in a namespace named for your file. Two names in
		// two DIFFERENT named namespaces never collide, which is why the
		// Tools/*Registration.cpp family keeps its RevvyXxxTool namespaces.
		
		// Keep explicit debug configurations easy to inspect. Development is the
		// distributed Editor binary and must retain UBT's normal optimization.
		if (Target.Configuration == UnrealTargetConfiguration.Debug || 
		    Target.Configuration == UnrealTargetConfiguration.DebugGame)
		{
			OptimizeCode = CodeOptimization.Never;
		}

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Networking",
				"Sockets",
				"HTTP",
				"Json",
				"JsonUtilities",
				"DeveloperSettings",
				"ApplicationCore",      // For FPlatformApplicationMisc (clipboard, etc.)
				"WebSockets",           // For ElevenLabs WebSocket connection
				"IKRig"                 // Runtime: UIKRigDefinition, UIKRetargeter, ERetargetSourceOrTarget, FIKRetargetProcessor
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"EditorScriptingUtilities",
				"EditorSubsystem",
				"PythonScriptPlugin",   // For Python code execution
				"Slate",
				"SlateCore",
				"UMG",
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"Projects",
				"AssetRegistry",
				"MessageLog",
				"MovieScene",
				"MovieSceneTracks",
				"MovieSceneTools",           // For Sequencer track/section editing helpers
				"LevelSequence",             // For ULevelSequence + ULevelSequenceFactoryNew (in editor target)
				"LevelSequenceEditor",       // For ULevelSequenceEditorSubsystem::AddActors and MovieSceneBindingProxy
				"Sequencer",                 // For Sequencer editor integration used by MovieRenderPipelineEditor
				"CinematicCamera",           // For ACineCameraActor + ACameraRig_Rail
				"MovieRenderPipelineCore",   // For UMoviePipelineQueueSubsystem, UMoviePipelineQueue, UMoviePipelinePrimaryConfig (MRQ2)
				"MovieRenderPipelineEditor", // For UMoviePipelinePIEExecutor (MRQ2 editor execution)
				"RenderCore",
				"RHI",                    // For GMaxRHIShaderPlatform / GShaderPlatformForFeatureLevel (material diagnostics)
				"EditorStyle",
				"AssetTools",
				"EnhancedInput",          // For Enhanced Input System support
				"AudioCapture",           // For microphone input
				"AudioCaptureCore",       // For FAudioCaptureSynth
				"ImageWrapper",           // For image encoding/decoding
				"DesktopPlatform",        // For file dialogs
				"Niagara",                // For Niagara VFX runtime classes
				"NiagaraEditor",          // For Niagara editor utilities and factories
				"AnimGraph",              // For AnimGraphNode types (state machines, states)
				"AnimGraphRuntime",       // For animation runtime types
				"Persona",                // For IAnimationBlueprintEditor interface
				"SkeletalMeshModifiers",  // For SkeletonModifier (bone manipulation) - MeshModelingToolset plugin
				"SkeletalMeshEditor",     // For SkeletalMeshEditorSubsystem
				"SkeletalMeshUtilitiesCommon", // For FStaticToSkeletalMeshConverter (rigid StaticMesh -> existing Skeleton recovery)
				"InputBlueprintNodes",    // For UK2Node_EnhancedInputAction (Enhanced Input event nodes)
				"Landscape",              // For ALandscape, ULandscapeInfo, ULandscapeComponent
				"LandscapeEditor",        // For landscape editor factories and utilities
				"Foliage",                // Required by LandscapeEdit.h (InstancedFoliageActor)
				"ModelViewViewModel",     // For MVVM ViewModel base classes (UMVVMViewModelBase)
				"ModelViewViewModelBlueprint", // For MVVM Blueprint View and bindings
				"StateTreeModule",        // For UStateTree, UStateTreeEditorData, StateTree core types
				"StateTreeEditorModule",  // For FStateTreeCompiler, UStateTreeState, StateTree editor types
				"PropertyBindingUtils",    // For FPropertyBindingBindableStructDescriptor (base of FStateTreeBindableStructDesc)
				"GameplayTags",           // For FGameplayTag (required by StateTree)
				"AudioEditor",            // For SoundCue graph classes (USoundCueGraphNode, USoundCueGraph, USoundCueFactoryNew)
				"MetasoundEngine",        // For UMetaSoundSource, UMetaSoundBuilderSubsystem, UMetaSoundSourceBuilder
				"MetasoundFrontend",      // For FMetaSoundFrontendDocumentBuilder, FMetasoundFrontendClassName, ISearchEngine
				"MetasoundGraphCore",     // For core MetaSound graph types
				"MeshDescription",        // For FMeshDescription / TVertexInstanceAttributesRef (UV editing)
				"StaticMeshDescription",  // For FStaticMeshAttributes / FStaticMeshOperations / FUVMapParameters
				"ControlRig",             // For FAnimNode_ControlRig / FControlRigAssetStrongReference / UControlRig (runtime types authored on UAnimGraphNode_ControlRig)
				"TraceLog",               // Runtime: UE_TRACE_* channel/event macros (defensive dep for UPerformanceService trace authoring)
				"PhysicsCore",            // Explicit for UPhysicsAssetService: FBodyInstance / UBodySetup Chaos interop types (transitive via Engine but recommended explicit)
				"NavigationSystem",       // Runtime: UNavigationSystemV1, FNavigationSystem, ANavMeshBoundsVolume (for UNavigationService)
				"Navmesh",                // Runtime: ARecastNavMesh (for UNavigationService agent radius/height config)
				"AIModule",               // Runtime: UNavigationPath, FNavPathPoint (for UNavigationService path query)
				"Chooser",                // Runtime: UChooserTable, FChooserEvaluationContext, FAssetChooser/FClassChooser/FEvaluateChooser, UChooserFunctionLibrary (for UChooserService — requires "Chooser" plugin enabled in .uproject)
				"GeometryFramework",      // Runtime: UDynamicMesh base type (for UGeometryScriptService)
				"GeometryScriptingCore",  // Runtime: UGeometryScriptLibrary_MeshPrimitiveFunctions / MeshBooleanFunctions / MeshBasicEditFunctions (for UGeometryScriptService)
				"GameFeatures",           // Runtime: UGameFeaturesSubsystem, UGameFeatureData, UGameFeatureAction, EGameFeatureTargetState (for UGameFeatureService)
				"ModularGameplay",        // Runtime: GameFeatures companion (UGameFrameworkComponentManager) — required by Lyra-style GameFeature actions
				"GameplayAbilities",      // Runtime: UAbilitySystemComponent, UGameplayAbility, UGameplayEffect, FGameplayAttribute (for UGameplayAbilitySystemService) — requires "GameplayAbilities" plugin enabled in .uproject (Lyra defaults)
				"ClothingSystemRuntimeCommon",   // Runtime: UClothingAssetCommon (for UChaosClothService — Bind/UnbindFromSkeletalMesh, ClothConfigs TMap)
				"ClothingSystemRuntimeInterface",// Runtime: UClothingAssetBase interface returned by USkeletalMesh::GetMeshClothingAssets (for UChaosClothService)
				"ChaosCloth",                    // Runtime: UChaosClothConfig / FChaosClothWeightedValue / EClothMassMode (for UChaosClothService) — requires "ChaosCloth" plugin (enabled by default)
				"HairStrandsCore",               // Runtime: UGroomAsset / UGroomBindingAsset / UGroomComponent (for UGroomService) — HairStrandsInterface merged into Core in 5.8
				"MediaAssets",                   // Runtime: UMediaPlayer, UMediaSource, UMediaTexture, UFileMediaSource (for UMediaService)
				"Media",                         // Runtime: FMediaPlayerOptions / MediaPlayerFacade (for UMediaService)
				"PoseSearch",                    // Runtime: UPoseSearchDatabase, UPoseSearchSchema, FPoseSearchDatabaseAnimationAssetBase / FPoseSearchDatabaseAnimationAsset, FAsyncPoseSearchDatabasesManagement (for UMotionMatchingService) — requires "PoseSearch" plugin enabled in .uproject (not on by default)
				"PCG",                           // Runtime: UPCGGraph, UPCGComponent, UPCGSubsystem (for UPCGService) — requires "PCG" plugin enabled in .uproject (NOT enabled in UE_ProjectA.uproject as of authoring — enable before building)
				"OnlineSubsystem",               // Runtime: IOnlineSubsystem, IOnlineSessionPtr, FOnlineSessionSettings, FOnlineSessionSearch (for UOnlineSessionService)
				"OnlineSubsystemUtils",          // Runtime: OnlineSubsystem helper utilities (session/beacon helpers) used by UOnlineSessionService
			}
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"PropertyEditor",      // For widget property editing
					"ToolMenus",           // For editor UI
					"BlueprintEditorLibrary", // For Blueprint utilities
					"UMGEditor",           // For WidgetBlueprint.h and other UMG editor functionality
					"MaterialEditor",      // For material editor integration
					"LevelEditor",         // For global keyboard shortcuts
					"StatusBar",           // For panel drawer integration
					"ContentBrowser",      // For content browser selection queries
					"ContentBrowserData",  // For UContentBrowserService: UContentBrowserDataSubsystem, FContentBrowserItem/ItemData, EContentBrowserItemTypeFilter (CB2 virtual-path layer, editor-only)
					"MetasoundEditor",     // For UMetaSoundEditorSubsystem (FindOrBeginBuilding, BuildToAsset)
				"GameplayTagsEditor",  // For IGameplayTagsEditorModule (add/remove/rename tags at editor time)
					"ControlRigDeveloper", // For UAnimGraphNode_ControlRig (editor AnimGraph node UCLASS lives here, Type=UncookedOnly so must be editor-only)
					"IKRigEditor",         // For UIKRigController, UIKRigDefinitionFactory, UIKRetargeterController, UIKRetargetFactory, UIKRetargetBatchOperation
					"IKRigDeveloper",      // Editor-side IK rig developer utilities (per UE 5.8 IKRig plugin module layout)
					"PhysicsUtilities",    // For UPhysicsAssetService: FPhysicsAssetUtils / FPhysAssetCreateParams (Developer module, editor-only)
					"WorldPartitionEditor", // For UWorldPartitionService: editor-side World Partition helpers (loader adapters, editor bounds queries)
					"DataLayerEditor",      // For UWorldPartitionService: UDataLayerEditorSubsystem (AddActorToDataLayer, GetAllDataLayers)
					"GeometryScriptingEditor", // For UGeometryScriptService: UGeometryScriptLibrary_StaticMeshFunctions (DynamicMesh <-> StaticMesh asset conversion)
					"HairStrandsEditor",       // For UGroomService: UGroomBindingAssetFactory + editor-only binding Build() helpers (HairStrands plugin, editor module)
					"PoseSearchEditor",        // For UMotionMatchingService: editor-only PoseSearch helpers (Uncooked module of PoseSearch plugin)
					"PCGEditor",               // For UPCGService: editor-only PCG factory helpers (Uncooked module of PCG plugin) — requires "PCG" plugin enabled in .uproject
				}
			);
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Advapi32.lib");
		}
	}
}
