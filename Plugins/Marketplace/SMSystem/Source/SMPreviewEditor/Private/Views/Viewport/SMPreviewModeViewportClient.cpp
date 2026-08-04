// Copyright Recursoft LLC. All Rights Reserved.

#include "SMPreviewModeViewportClient.h"
#include "SMPreviewObject.h"
#include "Utilities/SMPreviewUtils.h"
#include "Views/Viewport/SSMPreviewModeViewportView.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintEditor.h"

#include "Utilities/SMBlueprintEditorUtils.h"

#include "AssetViewerSettings.h"
#include "AudioDevice.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "PackageTools.h"
#include "PreviewScene.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#include "UnrealWidget.h"
#include "Viewports.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Slate/SceneViewport.h"
#include "UObject/FindObjectFlags.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SMPreviewModeViewportClient"

FSMAdvancedPreviewScene::FSMAdvancedPreviewScene(const ConstructionValues& CVS, const TSharedPtr<FSMStateMachineBlueprintEditor>& InEditor, float InFloorOffset) :
	FAdvancedPreviewScene(CVS, InFloorOffset), CVSStored(CVS), LastTickTime(0.f), FloorOffset(InFloorOffset)
{
	BlueprintEditor = InEditor;
	check(BlueprintEditor.IsValid());

	BlueprintPtr = BlueprintEditor.Pin()->GetStateMachineBlueprint();
	
	bIsBPEditorInPreviewMode = false;
	
	OnBlueprintModeChangedHandle = BlueprintEditor.Pin()->OnModeSet().AddRaw(this, &FSMAdvancedPreviewScene::OnBlueprintModeSet);

	OnBlueprintActiveTabChangedHandle = FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(
		FOnActiveTabChanged::FDelegate::CreateRaw(this,&FSMAdvancedPreviewScene::OnActiveTabChanged));

	OnBlueprintActiveTabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
		FOnActiveTabChanged::FDelegate::CreateRaw(this, &FSMAdvancedPreviewScene::OnActiveTabChanged));
	
	GameViewportClient = nullptr;
	PreviewPackage = nullptr;

	// Add in our components manually for memory management.
	OurComponents.Add(DirectionalLight);
	OurComponents.Add(SkyLight);
	OurComponents.Add(LineBatcher);
	OurComponents.Add(SkyComponent);
	OurComponents.Add(PostProcessComponent);
	OurComponents.Add(FloorMeshComponent);
	
	SetupInitialPreviewWorld();
	UpdateScene(DefaultSettings->Profiles[CurrentProfileIndex]);

	WorldContext = GEngine->GetWorldContextFromWorld(OriginalWorld);
	check(WorldContext);

	WorldContext->WorldType = EWorldType::EditorPreview;
}

FSMAdvancedPreviewScene::~FSMAdvancedPreviewScene()
{
	ParentTabPtr.Reset();

	if (OnBlueprintActiveTabChangedHandle.IsValid())
	{
		FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(OnBlueprintActiveTabChangedHandle);
	}

	if (OnBlueprintActiveTabForegroundedHandle.IsValid())
	{
		FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(OnBlueprintActiveTabForegroundedHandle);
	}
	
	if (BlueprintEditor.IsValid())
	{
		if (OnBlueprintModeChangedHandle.IsValid())
		{
			BlueprintEditor.Pin()->OnModeSet().Remove(OnBlueprintModeChangedHandle);
		}
	}
	
	if (PreviewObject.IsValid())
	{
		if (OnPreviewWorldRefreshHandle.IsValid())
		{
			PreviewObject->OnWorldRefreshRequiredEvent.Remove(OnPreviewWorldRefreshHandle);
		}
		PreviewObject->ReleaseActorHandles();
	}
	
	RestoreOriginalWorld();
	if (GEngine)
	{
		// Only destroy context when we're no longer using any world.
		GEngine->DestroyWorldContext(PreviewWorld);
		WorldContext = nullptr;
	}
	DestroyPreviewWorld();

	if (PreviewPackage)
	{
		FString PackageFilename;
		if (FPackageName::DoesPackageExist(PreviewPackage->GetName(), &PackageFilename))
		{
			TArray<UPackage*> PackagesToDelete;
			PackagesToDelete.Add(PreviewPackage);

			// Let the package auto-saver know that it needs to ignore the deleted packages.
			GUnrealEd->GetPackageAutoSaver().OnPackagesDeleted(PackagesToDelete);

			PreviewPackage->SetDirtyFlag(false);

			// Unload the packages and collect garbage.
			UPackageTools::UnloadPackages(PackagesToDelete);
		}

		PreviewPackage = nullptr;
	}

	// In case the level outliner is showing our world perform a full refresh.
	FSMPreviewOutlinerUtils::RefreshLevelEditorOutliner(this);
}

void FSMAdvancedPreviewScene::Tick(float DeltaTime)
{
	bIsTicking = true;
	FAdvancedPreviewScene::Tick(DeltaTime);

	// Tick the preview scene world only if the simulation is running.
	if (!GIntraFrameDebuggingGameThread && PreviewObject.IsValid() && PreviewObject->IsSimulationRunning() && GetWorld())
	{
		GetWorld()->Tick(LEVELTICK_All, DeltaTime);
	}
	bIsTicking = false;
	OnPostTick().ExecuteIfBound();
}

bool FSMAdvancedPreviewScene::IsTickable() const
{
	const float VisibilityTimeThreshold = 0.25f;

	// The preview scene is tickable if any viewport can see it
	return LastTickTime == 0.0 ||	// Never been ticked
		FPlatformTime::Seconds() - LastTickTime <= VisibilityTimeThreshold;	// Ticked recently
}

void FSMAdvancedPreviewScene::AddComponent(UActorComponent* Component, const FTransform& LocalToWorld,
	bool bAttachToRoot)
{
	FAdvancedPreviewScene::AddComponent(Component, LocalToWorld, bAttachToRoot);

	if (OriginalWorld)
	{
		// Small hack so we only add our components after the original world has been created, or
		// iterating them later on destruction can be problematic.
		OurComponents.AddUnique(Component);
	}
}

void FSMAdvancedPreviewScene::RemoveComponent(UActorComponent* Component)
{
	FAdvancedPreviewScene::RemoveComponent(Component);
	OurComponents.Remove(Component);
}

void FSMAdvancedPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Do not call parent, we are managing all memory here. We have the same components
	// and we also add the preview world to root.
	//FAdvancedPreviewScene::AddReferencedObjects(Collector);
	
	Collector.AddReferencedObjects(OurComponents);

	if (PreviewWorld && PreviewWorld != OriginalWorld)
	{
		Collector.AddReferencedObject(PreviewWorld);
	}
}

void FSMAdvancedPreviewScene::SetSceneViewport(TSharedPtr<FSceneViewport> InSceneViewport)
{
	SceneViewportPtr = InSceneViewport;
}

void FSMAdvancedPreviewScene::FlagTickable()
{
	LastTickTime = FPlatformTime::Seconds();
}

void FSMAdvancedPreviewScene::CheckRefreshLevelOutliner()
{
	if (WorldContext)
	{
		/* Refresh level outliner */
		if (FSMPreviewOutlinerUtils::RefreshLevelEditorOutliner(this))
		{
			WorldContext->WorldType = EWorldType::EditorPreview;
		}
		else
		{
			bool bIsMouseOverWindow = false;
			if (bIsBPEditorInPreviewMode && ParentTabPtr.IsValid() && ParentTabPtr.Pin()->IsForeground())
			{
				// Check the owning tab is in the foreground.
				
				const TSharedPtr<SWindow> ParentWindow = ParentTabPtr.Pin()->GetParentWindow();
				if (ParentWindow.IsValid())
				{
					// Check the tab's owning window contains mouse coordinates.
					const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnPlatform();
					bIsMouseOverWindow = ParentWindow->IsScreenspaceMouseWithin(MousePosition);
				}
			}

			// World context as Editor allows actors to be selected from this window in an actor picker,
			// but could allow the world outliner to display the world too.
			if (bIsBPEditorInPreviewMode && bIsMouseOverWindow)
			{
				WorldContext->WorldType = EWorldType::Editor;
			}
			else
			{
				// We are not active right now.
				WorldContext->WorldType = EWorldType::EditorPreview;
			}
		}
	}
}

void FSMAdvancedPreviewScene::CloneOriginalWorldToPreviewWorld(USMBlueprint* InBlueprint)
{
	check(OriginalWorld);
	check(WorldContext);
	
	UWorld* ClonedWorld = FSMPreviewUtils::DuplicateWorldForSimulation(GetTransientPackage()->GetName(), OriginalWorld, InBlueprint, *WorldContext);
	check(ClonedWorld);
	{
		// Temporary rename of original world to avoid conflicts.
		const FString OriginalNameWhileRunning = FString::Printf(TEXT("PrevRunning_%s"), *OriginalWorldName);
		OriginalWorld->Rename(*OriginalNameWhileRunning, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors);
	}

	// The cloned world should be the same as the original so actor references are found.
	ClonedWorld->Rename(*OriginalWorldName, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors);

	if (UGameInstance* GameInstance = ClonedWorld->GetGameInstance())
	{
		ensureAlways(GameViewportClient == nullptr);
		
		// Viewport client creation. Needs to never be garbage collected since the outer doesn't contain a reference to it. We destroy it when we're done.
		GameViewportClient = NewObject<UGameViewportClient>(GEngine, GEngine->GameViewportClientClass, NAME_None, RF_Standalone | RF_MarkAsRootSet | RF_Transient);
		WorldContext->GameViewport = GameViewportClient;

		GameViewportClient->Init(*WorldContext, GameInstance);

		// The scene viewport's client remains our editor client. The association only grafts the game
		// client's layer manager into the editor viewport overlay so UMG can reach the preview window.
		GameViewportClient->AddAssociation(*SceneViewportPtr);

		GameInstance->Init();
	}
	
	SetPreviewWorld(ClonedWorld);

	const FURL Url;
	ClonedWorld->InitializeActorsForPlay(Url);

	if (UGameInstance* GameInstance = ClonedWorld->GetGameInstance())
	{
		FString Error;
		ULocalPlayer* Player = GameInstance->CreateLocalPlayer(0, Error, true);
		APlayerController* Controller = Player->GetPlayerController(ClonedWorld);
		check(Controller);
		if (APawn* Pawn = Controller->GetPawnOrSpectator())
		{
			// The player controller needs to be spawned to consume input, but the visible pawn isn't needed.
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
		}
	}
	ClonedWorld->BeginPlay();

	// Dirty flag can become set and we don't want this to save.
	ClonedWorld->PersistentLevel->GetPackage()->SetDirtyFlag(false);
}

void FSMAdvancedPreviewScene::RestoreOriginalWorld()
{
	if (PreviewWorld != OriginalWorld)
	{
		DestroyPreviewWorld();
	}

	if (OriginalWorld->GetName() != OriginalWorldName)
	{
		// Make sure original world is back to the correct name.
		OriginalWorld->Rename(*OriginalWorldName, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}
	
	SetPreviewWorld(OriginalWorld);
}

void FSMAdvancedPreviewScene::SetPreviewObject(USMPreviewObject* Object)
{
	if (PreviewObject.Get() != Object)
	{
		if (PreviewObject.IsValid() && OnPreviewWorldRefreshHandle.IsValid())
		{
			PreviewObject->OnWorldRefreshRequiredEvent.Remove(OnPreviewWorldRefreshHandle);
		}

		PreviewObject = MakeWeakObjectPtr<USMPreviewObject>(Object);
		OnPreviewWorldRefreshHandle = PreviewObject->OnWorldRefreshRequiredEvent.AddRaw(this, &FSMAdvancedPreviewScene::OnPreviewObjectWorldRefreshRequested);

		PreviewObject->BindActorDelegates();
	}
}

UObject* FSMAdvancedPreviewScene::GetContextObjectForCurrentWorld() const
{
	check(PreviewObject.IsValid());
	const UObject* Context = PreviewObject->GetContextObject();
	if (Context == nullptr)
	{
		return nullptr;
	}
	
	return StaticFindObjectFast(Context->GetClass(), GetWorld()->PersistentLevel, Context->GetFName(), EFindObjectFlags::ExactClass);
}

void FSMAdvancedPreviewScene::SetPreviewWorld(UWorld* InPreviewWorld)
{
	if (PreviewWorld != InPreviewWorld)
	{
		PreviewWorld = InPreviewWorld;

		if (WorldContext)
		{
			WorldContext->SetCurrentWorld(PreviewWorld);
		}
		if (PreviewWorld != OriginalWorld)
		{
			GEngine->WorldAdded(PreviewWorld);
		}
		LoadEnvironmentComponents();
		UpdateScene(DefaultSettings->Profiles[CurrentProfileIndex]);
	}
}

void FSMAdvancedPreviewScene::DestroyPreviewWorld()
{
	check(PreviewWorld);

	if (IsValid(PreviewWorld))
	{
		PreviewWorld->EndPlay(EEndPlayReason::EndPlayInEditor);
	}
	
	if (UGameInstance* GameInstance = PreviewWorld->GetGameInstance())
	{
		GameInstance->Shutdown();
	}

	if (GameViewportClient)
	{
		// We manage memory for the viewport client so let's destroy it.
		if (SceneViewportPtr.IsValid())
		{
			GameViewportClient->RemoveAssociation(*SceneViewportPtr);
		}
		GameViewportClient->RemoveFromRoot();
		GameViewportClient->ClearFlags(RF_Standalone);
		GameViewportClient->ConditionalBeginDestroy();
		GameViewportClient = nullptr;
	}
	
	if (GEngine)
	{
		if (FAudioDeviceHandle AudioDevice = PreviewWorld->GetAudioDevice())
		{
			AudioDevice->Flush(GetWorld(), false);
		}
	}
	
	// Remove all attached components. It's better to destroy everything now
	// with RemoveComponent so the parent's private components array is cleared.
	// Their destructor will iterate it otherwise and a chaos crash can occur.
	{
		while (OurComponents.Num() > 0)
		{
			UActorComponent* Component = OurComponents[0];
			RemoveComponent(Component);
		}
	}

	if (IsValid(PreviewWorld))
	{
		// Prevents logs from displaying this world during an ActorDestroy call.
		//PreviewWorld->WorldType = EWorldType::Inactive;

		// TODO preview: UE 5.6 had crash issues with our original handling, and we also may have an issue
		// with third party plugins utilizing chaos physics. This section is probably fine now, but how
		// we create our world may have to change.

		PreviewWorld->RemoveFromRoot();
		PreviewWorld->DestroyWorld(false, OriginalWorld);
		PreviewWorld->MarkObjectsPendingKill();
		PreviewWorld->ReleasePhysicsScene();
		
		PreviewWorld->ConditionalBeginDestroy();

		
		PreviewWorld = nullptr;
	}
}

void FSMAdvancedPreviewScene::LoadEnvironmentComponents()
{
	if (IsValid(DirectionalLight))
	{
		RemoveComponent(DirectionalLight);
	}
	if (IsValid(SkyLight))
	{
		RemoveComponent(SkyLight);
	}
	if (IsValid(LineBatcher))
	{
		RemoveComponent(LineBatcher);
	}
	if (IsValid(SkyComponent))
	{
		RemoveComponent(SkyComponent);
	}
	if (IsValid(PostProcessComponent))
	{
		RemoveComponent(PostProcessComponent);
	}
	if (IsValid(FloorMeshComponent))
	{
		RemoveComponent(FloorMeshComponent);
	}
	
	/*
	 * This is basically FAdvancedPreviewScene and PreviewScene constructors. We need to call them again when the simulation world starts.
	 * The `Components` property would help with this, but it's private!
	 */
	
	if (CVSStored.bDefaultLighting)
	{
		DirectionalLight = NewObject<UDirectionalLightComponent>(GetPackageForPreviewWorld(), NAME_None, RF_Transient);
		DirectionalLight->Intensity = CVSStored.LightBrightness;
		DirectionalLight->LightColor = FColor::White;
		AddComponent(DirectionalLight, FTransform(CVSStored.LightRotation));

		SkyLight = NewObject<USkyLightComponent>(GetPackageForPreviewWorld(), NAME_None, RF_Transient);
		SkyLight->bLowerHemisphereIsBlack = false;
		SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyLight->Intensity = CVSStored.SkyBrightness;
		SkyLight->Mobility = EComponentMobility::Movable;
		AddComponent(SkyLight, FTransform::Identity);

		LineBatcher = NewObject<ULineBatchComponent>(GetPackageForPreviewWorld());
		LineBatcher->bCalculateAccurateBounds = false;
		AddComponent(LineBatcher, FTransform::Identity);
	}
	
	CurrentProfileIndex = DefaultSettings->Profiles.IsValidIndex(CurrentProfileIndex) ? GetDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex : 0;
	ensureMsgf(DefaultSettings->Profiles.IsValidIndex(CurrentProfileIndex), TEXT("Invalid default settings pointer or current profile index"));
	FPreviewSceneProfile& Profile = DefaultSettings->Profiles[CurrentProfileIndex];
	Profile.LoadEnvironmentMap();

	const FTransform Transform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(1));

	// Always set up sky light using the set cube map texture, reusing the sky light from PreviewScene class
	SetSkyCubemap(Profile.EnvironmentCubeMap.Get());
	SetSkyBrightness(Profile.SkyLightIntensity);

	// Large scale to prevent sphere from clipping
	const FTransform SphereTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(2000));
	SkyComponent = NewObject<UStaticMeshComponent>(GetPackageForPreviewWorld());

	// Set up sky sphere showing hte same cube map as used by the sky light
	UStaticMesh* SkySphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/AssetViewer/Sphere_inversenormals.Sphere_inversenormals"), nullptr, LOAD_None, nullptr);
	check(SkySphere);
	SkyComponent->SetStaticMesh(SkySphere);
	SkyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyComponent->bVisibleInRayTracing = false;

	UMaterial* SkyMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorMaterials/AssetViewer/M_SkyBox.M_SkyBox"), nullptr, LOAD_None, nullptr);
	check(SkyMaterial);

	InstancedSkyMaterial = NewObject<UMaterialInstanceConstant>(GetPackageForPreviewWorld());
	InstancedSkyMaterial->Parent = SkyMaterial;

	UTextureCube* DefaultTexture = LoadObject<UTextureCube>(nullptr, TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"));

	InstancedSkyMaterial->SetTextureParameterValueEditorOnly(FName("SkyBox"), (Profile.EnvironmentCubeMap.Get() != nullptr) ? Profile.EnvironmentCubeMap.Get() : DefaultTexture);
	InstancedSkyMaterial->SetScalarParameterValueEditorOnly(FName("CubemapRotation"), Profile.LightingRigRotation / 360.0f);
	InstancedSkyMaterial->SetScalarParameterValueEditorOnly(FName("Intensity"), Profile.SkyLightIntensity);
	InstancedSkyMaterial->PostLoad();
	SkyComponent->SetMaterial(0, InstancedSkyMaterial);
	AddComponent(SkyComponent, SphereTransform);

	PostProcessComponent = NewObject<UPostProcessComponent>(GetPackageForPreviewWorld());
	PostProcessComponent->Settings = Profile.PostProcessingSettings;
	PostProcessComponent->bUnbound = true;
	AddComponent(PostProcessComponent, Transform);

	UStaticMesh* FloorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/AssetViewer/Floor_Mesh.Floor_Mesh"), nullptr, LOAD_None, nullptr);
	check(FloorMesh);
	FloorMeshComponent = NewObject<UStaticMeshComponent>(GetPackageForPreviewWorld());
	FloorMeshComponent->SetStaticMesh(FloorMesh);

	FTransform FloorTransform(FRotator(0, 0, 0), FVector(0, 0, -(FloorOffset)), FVector(4.0f, 4.0f, 1.0f));
	AddComponent(FloorMeshComponent, FloorTransform);

	SetLightDirection(Profile.DirectionalLightRotation);

	bRotateLighting = Profile.bRotateLightingRig;
	CurrentRotationSpeed = Profile.RotationSpeed;
	bSkyChanged = false;
}

void FSMAdvancedPreviewScene::SetupInitialPreviewWorld()
{
	check(BlueprintEditor.IsValid());
	if (USMBlueprint* Blueprint = BlueprintEditor.Pin()->GetStateMachineBlueprint())
	{
		const FString NewWorldName = FString::Printf(TEXT("World_%s"), *Blueprint->GetName());
		
		const FString PackageName = FSMPreviewUtils::GetPreviewPackagePrefix();
		const FString PackageAssetPath = FPackageName::GetLongPackagePath(PackageName);
		const FString NewPackageName = FString::Printf(TEXT("/SMSystem/%s/%s"), *PackageAssetPath, *NewWorldName);
		
		PreviewPackage = CreatePackage(*NewPackageName);
		check(PreviewPackage);
		
		PreviewPackage->SetFlags(RF_Transient);
		PreviewPackage->MarkAsFullyLoaded();
		
		bool bSafeToRename = true;
		if (UWorld* ExistingWorld = Cast<UWorld>(StaticFindObjectFast(UWorld::StaticClass(), PreviewPackage, *NewWorldName, EFindObjectFlags::ExactClass)))
		{
			if (ensureAlwaysMsgf(!ExistingWorld->HasAnyFlags(RF_Standalone), TEXT("Existing world for blueprint found and is currently active when it should have been destroyed.")))
			{
				// This already exists, likely just not garbage collected yet.
				FSMBlueprintEditorUtils::TrashObject(ExistingWorld);
			}
			else
			{
				bSafeToRename = false;
			}
		}

		if (bSafeToRename)
		{
			// Rename to a friendly display name. Spaces are NOT allowed.
			PreviewWorld->Rename(*NewWorldName, PreviewPackage, REN_DoNotDirty | REN_DontCreateRedirectors | REN_AllowPackageLinkerMismatch);
		}
	}

	PreviewWorld->AddToRoot();
	OriginalWorld = PreviewWorld;
	// This world shouldn't save to disk.
	OriginalWorld->SetFlags(RF_Transient);
	OriginalWorldName = OriginalWorld->GetName();
}

void FSMAdvancedPreviewScene::OnPreviewObjectWorldRefreshRequested(USMPreviewObject* InPreviewObject)
{
	AWorldSettings* WorldSettings = OriginalWorld->GetWorldSettings();
	const TSubclassOf<AGameModeBase> GameModeClass = InPreviewObject->GetGameMode();
	WorldSettings->DefaultGameMode = GameModeClass.Get() ? GameModeClass.Get() : AGameModeBase::StaticClass();
	const TSubclassOf<UGameInstance> GameInstanceClass = InPreviewObject->GetGameInstance();
	UGameInstance* GameInstance =
		NewObject<UGameInstance>(GEngine, GameInstanceClass.Get() ? GameInstanceClass.Get() : UGameInstance::StaticClass());
	if (WorldContext)
	{
		GameInstance->OnWorldChanged(nullptr, WorldContext->World());
	}
	OriginalWorld->SetGameInstance(GameInstance);
	
	if (WorldContext)
	{
		WorldContext->OwningGameInstance = OriginalWorld->GetGameInstance();
	}
}

void FSMAdvancedPreviewScene::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive,
	TSharedPtr<SDockTab> NewlyActivated)
{
	if (NewlyActivated.IsValid() && BlueprintPtr.IsValid())
	{
		const TSharedPtr<FTabManager> TabManager = NewlyActivated->GetTabManagerPtr();
		if (TabManager.IsValid() && FSMPreviewOutlinerUtils::DoesTabBelongToPreview(TabManager, BlueprintPtr.Get()))
		{
			ParentTabPtr = TabManager->GetOwnerTab();
		}
	}
}

void FSMAdvancedPreviewScene::OnBlueprintModeSet(FName NewMode)
{
	bIsBPEditorInPreviewMode = NewMode == TEXT("PreviewMode");
	CheckRefreshLevelOutliner();
}

FSMPreviewModeViewportClient::FSMPreviewModeViewportClient(FSMAdvancedPreviewScene& InPreviewScene,
                                                           const TSharedRef<SSMPreviewModeViewportView>& InPreviewViewport) :
	FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InPreviewViewport))
{
	SelectedActor = nullptr;
	ThumbnailOwner = nullptr;
	bDraggingActor = false;
	bCaptureThumbnail = false;
	ScopedTransaction = nullptr;
	
	ViewportViewPtr = InPreviewViewport;

	EngineShowFlags.SetLumenReflections(false);
	EngineShowFlags.SetLumenGlobalIllumination(false);
	EngineShowFlags.Grid = false;
	
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = IsSetShowGridChecked();
	DrawHelper.PerspectiveGridSize = static_cast<float>(HALF_WORLD_MAX1);

	check(Widget);
	Widget->SetSnapEnabled(true);
	
	ShowWidget(true);
	GetModeTools()->SetSupportsViewportITF(false);

	SetViewMode(VMI_Lit);
	
	ViewportType = LVT_Perspective;
	bSetListenerPosition = false;
	SetRealtime(true);
	SetShowStats(true);

	//This seems to be needed to get the correct world time in the preview.
	SetIsSimulateInEditorViewport(true);

	ResetCamera();
}

FSMPreviewModeViewportClient::~FSMPreviewModeViewportClient()
{
	EndTransaction();
	
	if (FSMAdvancedPreviewScene* PrevScene = GetOurPreviewScene())
	{
		if (USMPreviewObject* PreviewObject = PrevScene->GetPreviewObject())
		{
			if (OnSimStartHandle.IsValid())
			{
				PreviewObject->OnSimulationStartedEvent.Remove(OnSimStartHandle);
			}
			if (OnSimEndHandle.IsValid())
			{
				PreviewObject->OnSimulationEndedEvent.Remove(OnSimEndHandle);
			}
		}
	}
	
	OnThumbnailCaptured.Unbind();
	ThumbnailOwner.Reset();
}

FLinearColor FSMPreviewModeViewportClient::GetBackgroundColor() const
{
	return FLinearColor::Gray;
}

UWorld* FSMPreviewModeViewportClient::GetWorld() const
{
	// We either want to return the preview scene world or nothing. The parent method will return GWorld as a fallback.
	if (PreviewScene)
	{
		return PreviewScene->GetWorld();
	}

	return nullptr;
}

void FSMPreviewModeViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	StaticCast<FSMAdvancedPreviewScene*>(PreviewScene)->FlagTickable();
}

void FSMPreviewModeViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FEditorViewportClient::Draw(InViewport, Canvas);
	DrawSimulating(InViewport, Canvas);
}

bool FSMPreviewModeViewportClient::ProcessScreenShots(FViewport* InViewport)
{
	if (bCaptureThumbnail && ThumbnailOwner.IsValid() && OnThumbnailCaptured.IsBound())
	{
		const int32 SrcWidth = InViewport->GetSizeXY().X;
		const int32 SrcHeight = InViewport->GetSizeXY().Y;

		// Read the contents of the InViewport into an array.
		TArray<FColor> OrigBitmap;
		if (InViewport->ReadPixels(OrigBitmap))
		{
			check(OrigBitmap.Num() == SrcWidth * SrcHeight);

			// Resize image to enforce max size.
			TArray<FColor> ScaledBitmap;
			const int32 ScaledWidth = ThumbnailCaptureSize.X;
			const int32 ScaledHeight = ThumbnailCaptureSize.Y;
			FImageUtils::CropAndScaleImage(SrcWidth, SrcHeight, ScaledWidth, ScaledHeight, OrigBitmap, ScaledBitmap);

			// Compress.
			FCreateTexture2DParameters Params;
			Params.bDeferCompression = true;

			UTexture2D* ThumbnailImage = FImageUtils::CreateTexture2D(ScaledWidth, ScaledHeight, ScaledBitmap, ThumbnailOwner.Get(), TEXT("ThumbnailTexture"), RF_NoFlags, Params);

			OnThumbnailCaptured.Execute(ThumbnailImage);
		}

		bCaptureThumbnail = false;
		return true;
	}

	return FEditorViewportClient::ProcessScreenShots(InViewport);
}

UE::Widget::EWidgetMode FSMPreviewModeViewportClient::GetWidgetMode() const
{
	if (IsActorSelected())
	{
		return FEditorViewportClient::GetWidgetMode();
	}

	return UE::Widget::WM_None;
}

FVector FSMPreviewModeViewportClient::GetWidgetLocation() const
{
	if (IsActorSelected())
	{
		return SelectedActor->GetActorLocation();
	}

	return FEditorViewportClient::GetWidgetLocation();
}

bool FSMPreviewModeViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (FSMAdvancedPreviewScene* OurPreviewScene = GetOurPreviewScene())
	{
		USMPreviewObject* PreviewObject = OurPreviewScene->GetPreviewObject();
		check(PreviewObject);
		if (PreviewObject->IsSimulationRunning())
		{
			if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed)
			{
				// Cancel out of a simulation.
				if (USMBlueprint* BlueprintOwner = Cast<USMBlueprint>(PreviewObject->GetOuter()))
				{
					FSMPreviewUtils::StopSimulation(BlueprintOwner);
					return true;
				}
			}
			
			if (UWorld* World = OurPreviewScene->GetPreviewWorld())
			{
				// Check for a player controller to send input to.
				for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
				{
					if (APlayerController* PlayerController = Iterator->Get())
					{
						if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
						{
							if (LocalPlayer->GetControllerId() == EventArgs.ControllerId)
							{
								PlayerController->InputKey(EventArgs);
								break;
							}
						}
					}
				}
			}
		}
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

bool FSMPreviewModeViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag,
                                                    FRotator& Rot, FVector& Scale)
{
	if (IsActorSelected() && bDraggingActor)
	{
		GEditor->ApplyDeltaToActor(SelectedActor.Get(), true, &Drag, &Rot, &Scale);
		return true;
	}

	return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
}

void FSMPreviewModeViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget,
	bool bNudge)
{
	if (!bDraggingActor && bIsDraggingWidget && InInputState.IsLeftMouseButtonPressed() && IsActorSelected())
	{
		GEditor->DisableDeltaModification(true);
		{
			// The pivot location won't update properly and the actor will rotate / move around the original selection origin
			// so update it here to fix that.
			GUnrealEd->UpdatePivotLocationForSelection();
			GUnrealEd->SetPivotMovedIndependently(false);
		}

		BeginTransaction(NSLOCTEXT("LogicDriverPreview", "ModifyPreviewActor", "Modify a Preview Actor"));
		bDraggingActor = true;
	}
	FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
}

void FSMPreviewModeViewportClient::TrackingStopped()
{
	bDraggingActor = false;
	EndTransaction();

	if (IsActorSelected())
	{
		GEditor->DisableDeltaModification(false);
	}
	
	FEditorViewportClient::TrackingStopped();
}

void FSMPreviewModeViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event,
                                                uint32 HitX, uint32 HitY)
{
	FSMPreviewUtils::DeselectEngineLevelEditor();
	
	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* HitActor = static_cast<HActor*>(HitProxy);
		SelectActor(HitActor->Actor);
		return;
	}

	SelectActor(nullptr);
	
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FSMPreviewModeViewportClient::SetSceneViewport(TSharedPtr<FSceneViewport> InViewport)
{
	SceneViewportPtr = InViewport;
}

void FSMPreviewModeViewportClient::SelectActor(AActor* NewActor)
{
	FSMAdvancedPreviewScene* OurPreviewScene = GetOurPreviewScene();
	check(OurPreviewScene);

	USMPreviewObject* PreviewObject = OurPreviewScene->GetPreviewObject();
	check(PreviewObject);
	
	// Track sim changes here since preview object isn't available in constructor.
	if (!OnSimStartHandle.IsValid())
	{
		OnSimStartHandle = PreviewObject->OnSimulationStartedEvent.AddRaw(this, &FSMPreviewModeViewportClient::OnSimulationStarted);
	}
	if (!OnSimEndHandle.IsValid())
	{
		OnSimEndHandle = PreviewObject->OnSimulationEndedEvent.AddRaw(this, &FSMPreviewModeViewportClient::OnSimulationEnded);
	}
	
	SelectedActor = NewActor;

	GEditor->SelectNone(true, true, false);
	
	if (SelectedActor.IsValid() && PreviewObject->ContainsActor(SelectedActor.Get()))
	{
		GEditor->SelectActor(NewActor, true, true);
		SetWidgetMode(UE::Widget::EWidgetMode::WM_Translate);
	}
}

void FSMPreviewModeViewportClient::ResetSelection()
{
	SelectActor(nullptr);
	SetWidgetMode(UE::Widget::WM_None);
}

void FSMPreviewModeViewportClient::ResetCamera()
{
	ToggleOrbitCamera(false);
	SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);
}

bool FSMPreviewModeViewportClient::GetShowGrid() const
{
	return IsSetShowGridChecked();
}

void FSMPreviewModeViewportClient::ToggleShowGrid()
{
	SetShowGrid();
	DrawHelper.bDrawGrid = EngineShowFlags.Grid;
}

void FSMPreviewModeViewportClient::CaptureThumbnail(UObject* InOwner, FOnThumbnailCaptured InOnThumbnailCaptured, FIntPoint InCaptureSize)
{
	ThumbnailCaptureSize = InCaptureSize;
	OnThumbnailCaptured = InOnThumbnailCaptured;
	ThumbnailOwner = InOwner;
	
	bCaptureThumbnail = true;

	if (SceneViewportPtr.IsValid())
	{
		FSceneViewport* SceneViewport = SceneViewportPtr.Pin().Get();
		ProcessScreenShots(SceneViewport);
	}
}

void FSMPreviewModeViewportClient::OnEditorTick(float DeltaTime)
{
	if (FSMAdvancedPreviewScene* AdvPreviewScene = GetOurPreviewScene())
	{
		AdvPreviewScene->CheckRefreshLevelOutliner();
	}
}

void FSMPreviewModeViewportClient::BeginTransaction(const FText& Description)
{
	if (!ScopedTransaction)
	{
		ScopedTransaction = new FScopedTransaction(Description);
	}
}

void FSMPreviewModeViewportClient::EndTransaction()
{
	if (ScopedTransaction)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

void FSMPreviewModeViewportClient::DrawSimulating(FViewport* InViewport, FCanvas* Canvas)
{
	if (FSMAdvancedPreviewScene* PrevScene = GetOurPreviewScene())
	{
		if (USMPreviewObject* PreviewObject = PrevScene->GetPreviewObject())
		{
			if (PreviewObject->IsSimulationRunning())
			{
				UFont* Font = GEngine->GetTinyFont();

				const FString DisplayText = "SIMULATING";

				int32 TextWidth, TextHeight;
				StringSize(Font, TextWidth, TextHeight, *DisplayText);

				float DPIScale = UpdateViewportClientWindowDPIScale();
				DPIScale = DPIScale > 0 ? DPIScale : 1.f;
				
				const FLinearColor Color = FLinearColor::Red;
				const FIntPoint Position(3, InViewport->GetSizeXY().Y / DPIScale - TextHeight);
				
				FCanvasTextItem TextItem(Position, FText::FromString(DisplayText), Font, Color);
				TextItem.Draw(Canvas);
			}
		}
	}
}

void FSMPreviewModeViewportClient::OnSimulationStarted(USMPreviewObject* PreviewObject)
{
	ResetSelection();
}

void FSMPreviewModeViewportClient::OnSimulationEnded(USMPreviewObject* PreviewObject)
{
	ResetSelection();
}

#undef LOCTEXT_NAMESPACE
