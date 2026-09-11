// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGreyboxService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGreybox, Log, All);

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

#include "Math/Box.h"

// =================================================================
// Constants
// =================================================================

// /Engine/BasicShapes/Cube is a 100 uu cube, so world-space Size in cm
// converts to actor scale via scale = Size / kCubeUnitSize.
static constexpr float kCubeUnitSize = 100.0f;

static const TCHAR* kCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

// =================================================================
// Helpers
// =================================================================

FString UGreyboxService::ApplyActorLabel(AActor* Actor, const FString& DesiredName)
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

UStaticMesh* UGreyboxService::LoadStaticMeshOrCube(const FString& AssetPath)
{
	if (!AssetPath.IsEmpty())
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath))
		{
			return Mesh;
		}
		UE_LOG(LogRevvyGreybox, Warning,
			TEXT("LoadStaticMeshOrCube: failed to load '%s', falling back to /Engine/BasicShapes/Cube"), *AssetPath);
	}
	return LoadObject<UStaticMesh>(nullptr, kCubePath);
}

UMaterialInterface* UGreyboxService::LoadMaterialOrNull(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}
	return LoadObject<UMaterialInterface>(nullptr, *AssetPath);
}

AActor* UGreyboxService::SpawnRootActor(const FString& RootLabel, FVector Location)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Root = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), SpawnParams);
	if (!Root)
	{
		return nullptr;
	}

	USceneComponent* RootComp = NewObject<USceneComponent>(Root, TEXT("DefaultSceneRoot"), RF_Transactional);
	if (RootComp)
	{
		Root->SetRootComponent(RootComp);
		Root->AddInstanceComponent(RootComp);
		RootComp->RegisterComponent();
	}

	Root->Modify();
	if (!RootLabel.IsEmpty())
	{
		Root->SetActorLabel(RootLabel);
	}
	return Root;
}

AStaticMeshActor* UGreyboxService::SpawnBoxInternal(const FString& Label, FVector Center, FVector BoxSize, UStaticMesh* Mesh, UMaterialInterface* Material)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Center), SpawnParams);
	if (!SMA)
	{
		return nullptr;
	}

	SMA->Modify();
	SMA->SetMobility(EComponentMobility::Movable);

	if (UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent())
	{
		Comp->Modify();
		if (Mesh)
		{
			Comp->SetStaticMesh(Mesh);
		}
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
	}

	const FVector Scale(
		BoxSize.X / kCubeUnitSize,
		BoxSize.Y / kCubeUnitSize,
		BoxSize.Z / kCubeUnitSize);
	SMA->SetActorScale3D(Scale);
	SMA->SetActorLocation(Center);

	if (!Label.IsEmpty())
	{
		SMA->SetActorLabel(Label);
	}
	return SMA;
}

// =================================================================
// Geometry Primitives
// =================================================================

FString UGreyboxService::SpawnGreyboxBox(const FString& Name, FVector Location, FVector Size, const FString& MaterialAssetPath)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxBox: no editor world available"));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxBox", "Spawn Greybox Box"));

	UStaticMesh* Cube = LoadStaticMeshOrCube(FString());
	UMaterialInterface* Mat = LoadMaterialOrNull(MaterialAssetPath);

	AStaticMeshActor* Box = SpawnBoxInternal(Name, Location, Size, Cube, Mat);
	if (!Box)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxBox: SpawnBoxInternal returned null"));
		return FString();
	}

	return ApplyActorLabel(Box, Name);
}

TArray<FString> UGreyboxService::SpawnGreyboxRoom(const FString& RoomName, FVector Center, FVector Size, float WallThickness, bool bIncludeCeiling)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxRoom: no editor world available"));
		return Labels;
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxRoom", "Spawn Greybox Room"));

	UStaticMesh* Cube = LoadStaticMeshOrCube(FString());

	const FString RootLabel = RoomName.IsEmpty() ? TEXT("GreyboxRoom") : RoomName;
	AActor* Root = SpawnRootActor(RootLabel + TEXT("_Root"), Center);
	if (!Root)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxRoom: root spawn failed"));
		return Labels;
	}
	Labels.Add(Root->GetActorLabel());

	// Floor at Center.Z - WallThickness/2, size = (Size.X, Size.Y, WallThickness)
	{
		const FVector FloorCenter(Center.X, Center.Y, Center.Z - WallThickness * 0.5f);
		const FVector FloorSize(Size.X + WallThickness * 2.0f, Size.Y + WallThickness * 2.0f, WallThickness);
		AStaticMeshActor* Floor = SpawnBoxInternal(RootLabel + TEXT("_Floor"), FloorCenter, FloorSize, Cube, nullptr);
		if (Floor)
		{
			Floor->SetActorLocation(FloorCenter);
			Floor->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Floor->GetActorLabel());
		}
	}

	// Wall N (+Y)
	{
		const FVector WCenter(Center.X, Center.Y + Size.Y * 0.5f + WallThickness * 0.5f, Center.Z + Size.Z * 0.5f);
		const FVector WSize(Size.X + WallThickness * 2.0f, WallThickness, Size.Z);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallN"), WCenter, WSize, Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WCenter);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}
	// Wall S (-Y)
	{
		const FVector WCenter(Center.X, Center.Y - Size.Y * 0.5f - WallThickness * 0.5f, Center.Z + Size.Z * 0.5f);
		const FVector WSize(Size.X + WallThickness * 2.0f, WallThickness, Size.Z);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallS"), WCenter, WSize, Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WCenter);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}
	// Wall E (+X)
	{
		const FVector WCenter(Center.X + Size.X * 0.5f + WallThickness * 0.5f, Center.Y, Center.Z + Size.Z * 0.5f);
		const FVector WSize(WallThickness, Size.Y, Size.Z);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallE"), WCenter, WSize, Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WCenter);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}
	// Wall W (-X)
	{
		const FVector WCenter(Center.X - Size.X * 0.5f - WallThickness * 0.5f, Center.Y, Center.Z + Size.Z * 0.5f);
		const FVector WSize(WallThickness, Size.Y, Size.Z);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallW"), WCenter, WSize, Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WCenter);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}

	if (bIncludeCeiling)
	{
		const FVector CCenter(Center.X, Center.Y, Center.Z + Size.Z + WallThickness * 0.5f);
		const FVector CSize(Size.X + WallThickness * 2.0f, Size.Y + WallThickness * 2.0f, WallThickness);
		AStaticMeshActor* Ceiling = SpawnBoxInternal(RootLabel + TEXT("_Ceiling"), CCenter, CSize, Cube, nullptr);
		if (Ceiling)
		{
			Ceiling->SetActorLocation(CCenter);
			Ceiling->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Ceiling->GetActorLabel());
		}
	}

	return Labels;
}

TArray<FString> UGreyboxService::SpawnGreyboxCorridor(const FString& Name, FVector Start, FVector End, float Width, float Height, float WallThickness)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxCorridor: no editor world available"));
		return Labels;
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxCorridor", "Spawn Greybox Corridor"));

	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length < KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxCorridor: Start and End coincide"));
		return Labels;
	}

	const FVector Center = (Start + End) * 0.5f;
	const FRotator Rot = Delta.Rotation();

	UStaticMesh* Cube = LoadStaticMeshOrCube(FString());

	const FString RootLabel = Name.IsEmpty() ? TEXT("GreyboxCorridor") : Name;
	AActor* Root = SpawnRootActor(RootLabel + TEXT("_Root"), Center);
	if (!Root)
	{
		return Labels;
	}
	Root->SetActorRotation(Rot);
	Labels.Add(Root->GetActorLabel());

	// Build local-space slabs; attach with KeepRelative so root rotation applies.
	// Floor: full length x width x WallThickness, sits below origin
	{
		const FVector Local(0.0f, 0.0f, -WallThickness * 0.5f);
		const FVector WorldCenter = Center + Rot.RotateVector(Local);
		AStaticMeshActor* Floor = SpawnBoxInternal(RootLabel + TEXT("_Floor"), WorldCenter, FVector(Length, Width, WallThickness), Cube, nullptr);
		if (Floor)
		{
			Floor->SetActorLocation(WorldCenter);
			Floor->SetActorRotation(Rot);
			Floor->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Floor->GetActorLabel());
		}
	}
	// Ceiling
	{
		const FVector Local(0.0f, 0.0f, Height + WallThickness * 0.5f);
		const FVector WorldCenter = Center + Rot.RotateVector(Local);
		AStaticMeshActor* Ceiling = SpawnBoxInternal(RootLabel + TEXT("_Ceiling"), WorldCenter, FVector(Length, Width, WallThickness), Cube, nullptr);
		if (Ceiling)
		{
			Ceiling->SetActorLocation(WorldCenter);
			Ceiling->SetActorRotation(Rot);
			Ceiling->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Ceiling->GetActorLabel());
		}
	}
	// Wall Left (+Y in local)
	{
		const FVector Local(0.0f, Width * 0.5f + WallThickness * 0.5f, Height * 0.5f);
		const FVector WorldCenter = Center + Rot.RotateVector(Local);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallLeft"), WorldCenter, FVector(Length, WallThickness, Height), Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WorldCenter);
			Wall->SetActorRotation(Rot);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}
	// Wall Right (-Y in local)
	{
		const FVector Local(0.0f, -Width * 0.5f - WallThickness * 0.5f, Height * 0.5f);
		const FVector WorldCenter = Center + Rot.RotateVector(Local);
		AStaticMeshActor* Wall = SpawnBoxInternal(RootLabel + TEXT("_WallRight"), WorldCenter, FVector(Length, WallThickness, Height), Cube, nullptr);
		if (Wall)
		{
			Wall->SetActorLocation(WorldCenter);
			Wall->SetActorRotation(Rot);
			Wall->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Wall->GetActorLabel());
		}
	}

	return Labels;
}

TArray<FString> UGreyboxService::SpawnGreyboxStairs(const FString& Name, FVector BasePos, int32 StepCount, float StepRise, float StepRun, float StepWidth, FRotator DirectionRot)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxStairs: no editor world available"));
		return Labels;
	}
	if (StepCount <= 0)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxStairs: StepCount must be > 0"));
		return Labels;
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxStairs", "Spawn Greybox Stairs"));

	UStaticMesh* Cube = LoadStaticMeshOrCube(FString());

	const FString RootLabel = Name.IsEmpty() ? TEXT("GreyboxStairs") : Name;
	AActor* Root = SpawnRootActor(RootLabel + TEXT("_Root"), BasePos);
	if (!Root)
	{
		return Labels;
	}
	Root->SetActorRotation(DirectionRot);
	Labels.Add(Root->GetActorLabel());

	for (int32 i = 0; i < StepCount; ++i)
	{
		// Each step: box of (StepRun, StepWidth, StepRise*(i+1)) whose top face is at i*StepRise
		const float Depth = StepRun;
		const float StepHeight = StepRise * (i + 1);
		const FVector Local(StepRun * (i + 0.5f), 0.0f, StepHeight * 0.5f);
		const FVector WorldCenter = BasePos + DirectionRot.RotateVector(Local);

		const FString StepLabel = FString::Printf(TEXT("%s_Step_%d"), *RootLabel, i);
		AStaticMeshActor* Step = SpawnBoxInternal(StepLabel, WorldCenter, FVector(Depth, StepWidth, StepHeight), Cube, nullptr);
		if (Step)
		{
			Step->SetActorLocation(WorldCenter);
			Step->SetActorRotation(DirectionRot);
			Step->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Labels.Add(Step->GetActorLabel());
		}
	}

	return Labels;
}

FString UGreyboxService::SpawnGreyboxRamp(const FString& Name, FVector Start, FVector End, float Width, float Thickness)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxRamp: no editor world available"));
		return FString();
	}

	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length < KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxRamp: Start and End coincide"));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxRamp", "Spawn Greybox Ramp"));

	UStaticMesh* Cube = LoadStaticMeshOrCube(FString());
	const FVector Center = (Start + End) * 0.5f;
	const FRotator Rot = Delta.Rotation();

	AStaticMeshActor* Ramp = SpawnBoxInternal(Name, Center, FVector(Length, Width, Thickness), Cube, nullptr);
	if (!Ramp)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxRamp: SpawnBoxInternal returned null"));
		return FString();
	}
	Ramp->SetActorLocation(Center);
	Ramp->SetActorRotation(Rot);

	return ApplyActorLabel(Ramp, Name);
}

// =================================================================
// Array / Distribution
// =================================================================

TArray<FString> UGreyboxService::SpawnGreyboxGrid(const FString& Name, FVector Origin, FVector2D CellSize, int32 GridX, int32 GridY, const FString& CellAssetPath)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxGrid: no editor world available"));
		return Labels;
	}
	if (GridX <= 0 || GridY <= 0)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("SpawnGreyboxGrid: GridX/GridY must be > 0"));
		return Labels;
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "SpawnGreyboxGrid", "Spawn Greybox Grid"));

	UStaticMesh* Mesh = LoadStaticMeshOrCube(CellAssetPath);

	const FString RootLabel = Name.IsEmpty() ? TEXT("GreyboxGrid") : Name;
	AActor* Root = SpawnRootActor(RootLabel + TEXT("_Root"), Origin);
	if (!Root)
	{
		return Labels;
	}
	Labels.Add(Root->GetActorLabel());

	for (int32 x = 0; x < GridX; ++x)
	{
		for (int32 y = 0; y < GridY; ++y)
		{
			const FVector CellCenter(
				Origin.X + x * CellSize.X,
				Origin.Y + y * CellSize.Y,
				Origin.Z);
			const FString CellLabel = FString::Printf(TEXT("%s_Cell_%d_%d"), *RootLabel, x, y);

			AStaticMeshActor* Cell = SpawnBoxInternal(CellLabel, CellCenter, FVector(CellSize.X, CellSize.Y, kCubeUnitSize), Mesh, nullptr);
			if (Cell)
			{
				Cell->SetActorLocation(CellCenter);
				Cell->AttachToActor(Root, FAttachmentTransformRules::KeepRelativeTransform);
				Labels.Add(Cell->GetActorLabel());
			}
		}
	}

	return Labels;
}

TArray<FString> UGreyboxService::ArrayGreyboxMeshes(const FString& BaseActorNameOrPath, int32 Count, FVector Spacing, const FString& AxisStr)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ArrayGreyboxMeshes: no editor world available"));
		return Labels;
	}
	if (Count <= 0)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ArrayGreyboxMeshes: Count must be > 0"));
		return Labels;
	}

	AActor* Source = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, BaseActorNameOrPath);
	if (!Source)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ArrayGreyboxMeshes: source '%s' not found"), *BaseActorNameOrPath);
		return Labels;
	}

	// Interpret axis mask
	const FString Axis = AxisStr.ToUpper();
	FVector Mask(0.0f);
	if (Axis.Contains(TEXT("X"))) Mask.X = 1.0f;
	if (Axis.Contains(TEXT("Y"))) Mask.Y = 1.0f;
	if (Axis.Contains(TEXT("Z"))) Mask.Z = 1.0f;
	if (Mask.IsNearlyZero())
	{
		Mask = FVector(1.0f, 0.0f, 0.0f);
	}
	const FVector EffectiveSpacing(Spacing.X * Mask.X, Spacing.Y * Mask.Y, Spacing.Z * Mask.Z);

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "ArrayGreyboxMeshes", "Array Greybox Meshes"));

	const FVector BaseLoc = Source->GetActorLocation();
	const FRotator BaseRot = Source->GetActorRotation();
	const FVector BaseScale = Source->GetActorScale3D();
	const FString BaseLabel = Source->GetActorLabel();

	AStaticMeshActor* SourceSMA = Cast<AStaticMeshActor>(Source);
	UStaticMesh* SourceMesh = nullptr;
	TArray<UMaterialInterface*> SourceMats;
	if (SourceSMA)
	{
		if (UStaticMeshComponent* SrcComp = SourceSMA->GetStaticMeshComponent())
		{
			SourceMesh = SrcComp->GetStaticMesh();
			const int32 NumMats = SrcComp->GetNumMaterials();
			for (int32 m = 0; m < NumMats; ++m)
			{
				SourceMats.Add(SrcComp->GetMaterial(m));
			}
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 1; i <= Count; ++i)
	{
		const FVector NewLoc = BaseLoc + EffectiveSpacing * i;
		AActor* Dup = nullptr;

		if (SourceSMA)
		{
			AStaticMeshActor* NewSMA = World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), FTransform(BaseRot, NewLoc, BaseScale), SpawnParams);
			if (NewSMA)
			{
				NewSMA->Modify();
				NewSMA->SetMobility(EComponentMobility::Movable);
				if (UStaticMeshComponent* DstComp = NewSMA->GetStaticMeshComponent())
				{
					DstComp->Modify();
					if (SourceMesh)
					{
						DstComp->SetStaticMesh(SourceMesh);
					}
					for (int32 m = 0; m < SourceMats.Num(); ++m)
					{
						if (SourceMats[m])
						{
							DstComp->SetMaterial(m, SourceMats[m]);
						}
					}
				}
				Dup = NewSMA;
			}
		}
		else
		{
			// Non-StaticMeshActor source: spawn a plain AActor at the transform.
			Dup = World->SpawnActor<AActor>(Source->GetClass(), FTransform(BaseRot, NewLoc, BaseScale), SpawnParams);
			if (Dup)
			{
				Dup->Modify();
			}
		}

		if (!Dup)
		{
			UE_LOG(LogRevvyGreybox, Warning, TEXT("ArrayGreyboxMeshes: duplicate #%d failed"), i);
			continue;
		}

		Dup->SetActorLabel(FString::Printf(TEXT("%s_%d"), *BaseLabel, i));
		Labels.Add(Dup->GetActorLabel());
	}

	return Labels;
}

// =================================================================
// Query / Bake
// =================================================================

FGreyboxBoundsResult UGreyboxService::GetGreyboxBounds(const TArray<FString>& ActorNamesOrPaths)
{
	FGreyboxBoundsResult Result;
	Result.bValid = false;
	Result.Bounds = FBox(ForceInit);

	FBox Combined(ForceInit);
	bool bAny = false;

	for (const FString& Q : ActorNamesOrPaths)
	{
		AActor* A = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, Q);
		if (!A) continue;

		FVector Origin, Extent;
		A->GetActorBounds(false, Origin, Extent, /*bIncludeFromChildActors*/ true);
		const FBox B(Origin - Extent, Origin + Extent);
		Combined += B;
		bAny = true;
	}

	if (!bAny)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("GetGreyboxBounds: no supplied actors were resolved"));
		return Result;
	}

	Result.bValid = true;
	Result.Bounds = Combined;
	Result.Origin = Combined.GetCenter();
	Result.Extent = Combined.GetExtent();
	return Result;
}

static void CollectDescendants(AActor* Root, TArray<AActor*>& Out)
{
	if (!Root) return;
	Out.Add(Root);
	TArray<AActor*> Children;
	Root->GetAttachedActors(Children);
	for (AActor* Child : Children)
	{
		CollectDescendants(Child, Out);
	}
}

FString UGreyboxService::ExportGreyboxToJSON(const FString& RootActorNameOrPath)
{
	AActor* Root = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, RootActorNameOrPath);
	if (!Root)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ExportGreyboxToJSON: root '%s' not found"), *RootActorNameOrPath);
		return FString();
	}

	TArray<AActor*> All;
	CollectDescendants(Root, All);

	const FTransform RootT = Root->GetActorTransform();

	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("root_label"), Root->GetActorLabel());
	RootObj->SetStringField(TEXT("root_class"), Root->GetClass()->GetPathName());

	TArray<TSharedPtr<FJsonValue>> Actors;
	for (AActor* A : All)
	{
		if (!A) continue;

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), A->GetActorLabel());
		Obj->SetStringField(TEXT("class"), A->GetClass()->GetPathName());

		const FTransform WT = A->GetActorTransform();
		const FTransform RT = WT.GetRelativeTransform(RootT);
		const FVector RL = RT.GetLocation();
		const FRotator RR = RT.Rotator();
		const FVector RS = RT.GetScale3D();

		TSharedRef<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), RL.X);
		LocObj->SetNumberField(TEXT("y"), RL.Y);
		LocObj->SetNumberField(TEXT("z"), RL.Z);
		Obj->SetObjectField(TEXT("rel_location"), LocObj);

		TSharedRef<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), RR.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), RR.Yaw);
		RotObj->SetNumberField(TEXT("roll"), RR.Roll);
		Obj->SetObjectField(TEXT("rel_rotation"), RotObj);

		TSharedRef<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), RS.X);
		ScaleObj->SetNumberField(TEXT("y"), RS.Y);
		ScaleObj->SetNumberField(TEXT("z"), RS.Z);
		Obj->SetObjectField(TEXT("rel_scale"), ScaleObj);

		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(A))
		{
			if (UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent())
			{
				if (UStaticMesh* SM = Comp->GetStaticMesh())
				{
					Obj->SetStringField(TEXT("static_mesh"), SM->GetPathName());
				}
				TArray<TSharedPtr<FJsonValue>> Mats;
				const int32 NumMats = Comp->GetNumMaterials();
				for (int32 m = 0; m < NumMats; ++m)
				{
					if (UMaterialInterface* MI = Comp->GetMaterial(m))
					{
						Mats.Add(MakeShared<FJsonValueString>(MI->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueString>(FString()));
					}
				}
				Obj->SetArrayField(TEXT("materials"), Mats);
			}
		}

		Actors.Add(MakeShared<FJsonValueObject>(Obj));
	}
	RootObj->SetArrayField(TEXT("actors"), Actors);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(RootObj, Writer);
	return Out;
}

TArray<FString> UGreyboxService::ImportGreyboxFromJSON(const FString& JsonString, const FString& NewRootName, FVector RootLocation)
{
	TArray<FString> Labels;

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ImportGreyboxFromJSON: no editor world"));
		return Labels;
	}

	TSharedPtr<FJsonObject> RootObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ImportGreyboxFromJSON: JSON parse failed"));
		return Labels;
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "ImportGreyboxFromJSON", "Import Greybox From JSON"));

	const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("actors"), Actors) || !Actors)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ImportGreyboxFromJSON: missing 'actors' field"));
		return Labels;
	}

	AActor* NewRoot = SpawnRootActor(NewRootName.IsEmpty() ? TEXT("GreyboxImport_Root") : NewRootName, RootLocation);
	if (!NewRoot)
	{
		return Labels;
	}
	Labels.Add(NewRoot->GetActorLabel());

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Skip the first record (assumed to be the exported root)
	bool bSkippedRoot = false;
	for (const TSharedPtr<FJsonValue>& V : *Actors)
	{
		if (!V.IsValid()) continue;
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!V->TryGetObject(ObjPtr) || !ObjPtr->IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		if (!bSkippedRoot)
		{
			bSkippedRoot = true;
			continue;
		}

		FString ClassPath;
		Obj->TryGetStringField(TEXT("class"), ClassPath);
		UClass* SpawnClass = AStaticMeshActor::StaticClass();
		if (!ClassPath.IsEmpty())
		{
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassPath))
			{
				SpawnClass = Loaded;
			}
		}

		FVector RL = FVector::ZeroVector, RS = FVector::OneVector;
		FRotator RR = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* Sub;
		if (Obj->TryGetObjectField(TEXT("rel_location"), Sub) && Sub->IsValid())
		{
			(*Sub)->TryGetNumberField(TEXT("x"), RL.X);
			(*Sub)->TryGetNumberField(TEXT("y"), RL.Y);
			(*Sub)->TryGetNumberField(TEXT("z"), RL.Z);
		}
		if (Obj->TryGetObjectField(TEXT("rel_rotation"), Sub) && Sub->IsValid())
		{
			(*Sub)->TryGetNumberField(TEXT("pitch"), RR.Pitch);
			(*Sub)->TryGetNumberField(TEXT("yaw"), RR.Yaw);
			(*Sub)->TryGetNumberField(TEXT("roll"), RR.Roll);
		}
		if (Obj->TryGetObjectField(TEXT("rel_scale"), Sub) && Sub->IsValid())
		{
			(*Sub)->TryGetNumberField(TEXT("x"), RS.X);
			(*Sub)->TryGetNumberField(TEXT("y"), RS.Y);
			(*Sub)->TryGetNumberField(TEXT("z"), RS.Z);
		}

		const FTransform RelT(RR, RL, RS);
		const FTransform WorldT = RelT * NewRoot->GetActorTransform();

		AActor* NewA = World->SpawnActor<AActor>(SpawnClass, WorldT, SpawnParams);
		if (!NewA)
		{
			UE_LOG(LogRevvyGreybox, Warning, TEXT("ImportGreyboxFromJSON: SpawnActor failed for class '%s'"), *ClassPath);
			continue;
		}

		NewA->Modify();
		NewA->SetActorTransform(WorldT);

		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(NewA))
		{
			SMA->SetMobility(EComponentMobility::Movable);
			if (UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent())
			{
				Comp->Modify();
				FString MeshPath;
				if (Obj->TryGetStringField(TEXT("static_mesh"), MeshPath) && !MeshPath.IsEmpty())
				{
					if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *MeshPath))
					{
						Comp->SetStaticMesh(SM);
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Mats = nullptr;
				if (Obj->TryGetArrayField(TEXT("materials"), Mats) && Mats)
				{
					for (int32 m = 0; m < Mats->Num(); ++m)
					{
						FString MatPath;
						if ((*Mats)[m].IsValid() && (*Mats)[m]->TryGetString(MatPath) && !MatPath.IsEmpty())
						{
							if (UMaterialInterface* MI = LoadObject<UMaterialInterface>(nullptr, *MatPath))
							{
								Comp->SetMaterial(m, MI);
							}
						}
					}
				}
			}
		}

		FString ChildLabel;
		Obj->TryGetStringField(TEXT("label"), ChildLabel);
		if (!ChildLabel.IsEmpty())
		{
			NewA->SetActorLabel(FString::Printf(TEXT("%s_%s"), *NewRoot->GetActorLabel(), *ChildLabel));
		}

		NewA->AttachToActor(NewRoot, FAttachmentTransformRules::KeepWorldTransform);
		Labels.Add(NewA->GetActorLabel());
	}

	return Labels;
}

FString UGreyboxService::ConvertGreyboxToStaticMesh(const FString& RootActorNameOrPath, const FString& OutputAssetPath)
{
	UE_LOG(LogRevvyGreybox, Warning,
		TEXT("ConvertGreyboxToStaticMesh: NAIVE fallback — duplicating first descendant's UStaticMesh; ")
		TEXT("no geometry merge is performed."));

	AActor* Root = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, RootActorNameOrPath);
	if (!Root)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ConvertGreyboxToStaticMesh: root '%s' not found"), *RootActorNameOrPath);
		return FString();
	}
	if (OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ConvertGreyboxToStaticMesh: OutputAssetPath is empty"));
		return FString();
	}

	TArray<AActor*> All;
	CollectDescendants(Root, All);

	UStaticMesh* SourceMesh = nullptr;
	for (AActor* A : All)
	{
		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(A))
		{
			if (UStaticMeshComponent* Comp = SMA->GetStaticMeshComponent())
			{
				if (UStaticMesh* SM = Comp->GetStaticMesh())
				{
					SourceMesh = SM;
					break;
				}
			}
		}
	}

	if (!SourceMesh)
	{
		UE_LOG(LogRevvyGreybox, Warning, TEXT("ConvertGreyboxToStaticMesh: no descendant with a UStaticMesh found"));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("GreyboxService", "ConvertGreyboxToStaticMesh", "Convert Greybox To Static Mesh"));

	const FString SourcePath = SourceMesh->GetPathName();
	if (UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, OutputAssetPath))
	{
		UEditorAssetLibrary::SaveLoadedAsset(Dup, /*bOnlyIfIsDirty*/ false);
		return Dup->GetPathName();
	}

	UE_LOG(LogRevvyGreybox, Warning,
		TEXT("ConvertGreyboxToStaticMesh: DuplicateAsset('%s' -> '%s') failed"),
		*SourcePath, *OutputAssetPath);
	return FString();
}
