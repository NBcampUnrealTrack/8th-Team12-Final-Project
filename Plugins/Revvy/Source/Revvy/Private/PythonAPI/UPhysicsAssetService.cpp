// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPhysicsAssetService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPhysAsset, Log, All);

// Editor / transaction / asset infrastructure
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

// Physics asset core types
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/BodyInstance.h"

// Skeletal mesh + skeleton
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"

// Physics utils (Developer/PhysicsUtilities)
#include "PhysicsAssetUtils.h"

// JSON serialization
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// EPhysicsType / EBodyCollisionResponse are exposed transitively via BodySetup.h

// ============================================================================
// Helpers
// ============================================================================

namespace
{
	UPhysicsAsset* LoadPhysicsAssetChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogRevvyPhysAsset, Warning, TEXT("%s: PhysAssetPath is empty"), CallSite);
			return nullptr;
		}
		UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(Path));
		if (!PhysAsset)
		{
			UE_LOG(LogRevvyPhysAsset, Warning, TEXT("%s: failed to load PhysicsAsset at '%s'"), CallSite, *Path);
		}
		return PhysAsset;
	}

	USkeletalBodySetup* FindBodyByBoneName(UPhysicsAsset* PhysAsset, const FName BoneName, int32* OutIndex = nullptr)
	{
		if (!PhysAsset)
		{
			return nullptr;
		}
		for (int32 Index = 0; Index < PhysAsset->SkeletalBodySetups.Num(); ++Index)
		{
			USkeletalBodySetup* Body = Cast<USkeletalBodySetup>(PhysAsset->SkeletalBodySetups[Index]);
			if (Body && Body->BoneName == BoneName)
			{
				if (OutIndex) { *OutIndex = Index; }
				return Body;
			}
		}
		return nullptr;
	}

	UPhysicsConstraintTemplate* FindConstraintByChildBone(UPhysicsAsset* PhysAsset, const FName ChildBoneName, int32* OutIndex = nullptr)
	{
		if (!PhysAsset)
		{
			return nullptr;
		}
		for (int32 Index = 0; Index < PhysAsset->ConstraintSetup.Num(); ++Index)
		{
			UPhysicsConstraintTemplate* Con = PhysAsset->ConstraintSetup[Index];
			if (!Con) continue;
			if (Con->DefaultInstance.ConstraintBone1 == ChildBoneName ||
				Con->DefaultInstance.JointName      == ChildBoneName)
			{
				if (OutIndex) { *OutIndex = Index; }
				return Con;
			}
		}
		return nullptr;
	}

	void SplitObjectPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName)
	{
		// Accept forms:
		//   "/Game/Path/Name"
		//   "/Game/Path/Name.Name"
		//   "Name" -> defaults package path to "/Game"
		FString Cleaned = FullPath;
		int32 DotIdx;
		if (Cleaned.FindChar('.', DotIdx))
		{
			Cleaned = Cleaned.Left(DotIdx);
		}

		int32 SlashIdx;
		if (Cleaned.FindLastChar('/', SlashIdx))
		{
			OutPackagePath = Cleaned.Left(SlashIdx);
			OutAssetName   = Cleaned.Mid(SlashIdx + 1);
		}
		else
		{
			OutPackagePath = TEXT("/Game");
			OutAssetName   = Cleaned;
		}

		if (OutPackagePath.IsEmpty())
		{
			OutPackagePath = TEXT("/Game");
		}
	}

	bool ParseGeomType(const FString& GeomTypeStr, EPhysAssetFitGeomType& OutType)
	{
		const FString S = GeomTypeStr.ToLower();
		if (S == TEXT("box"))                     { OutType = EFG_Box;              return true; }
		if (S == TEXT("sphere"))                  { OutType = EFG_Sphere;           return true; }
		if (S == TEXT("sphyl") || S == TEXT("capsule")) { OutType = EFG_Sphyl;      return true; }
		if (S == TEXT("taperedcapsule") || S == TEXT("tapered_capsule")) { OutType = EFG_TaperedCapsule; return true; }
		if (S == TEXT("singleconvexhull") || S == TEXT("single_convex_hull")) { OutType = EFG_SingleConvexHull; return true; }
		if (S == TEXT("multiconvexhull")  || S == TEXT("multi_convex_hull"))  { OutType = EFG_MultiConvexHull;  return true; }
		return false;
	}

	bool ParseAngularMotion(const FString& S, EAngularConstraintMotion& OutMotion)
	{
		const FString L = S.ToLower();
		if (L == TEXT("free"))    { OutMotion = ACM_Free;    return true; }
		if (L == TEXT("limited")) { OutMotion = ACM_Limited; return true; }
		if (L == TEXT("locked"))  { OutMotion = ACM_Locked;  return true; }
		return false;
	}

	bool ParseLinearMotion(const FString& S, ELinearConstraintMotion& OutMotion)
	{
		const FString L = S.ToLower();
		if (L == TEXT("free"))    { OutMotion = LCM_Free;    return true; }
		if (L == TEXT("limited")) { OutMotion = LCM_Limited; return true; }
		if (L == TEXT("locked"))  { OutMotion = LCM_Locked;  return true; }
		return false;
	}

	const TCHAR* AngularMotionToString(EAngularConstraintMotion M)
	{
		switch (M)
		{
			case ACM_Free:    return TEXT("Free");
			case ACM_Limited: return TEXT("Limited");
			case ACM_Locked:  return TEXT("Locked");
			default:          return TEXT("Unknown");
		}
	}

	const TCHAR* LinearMotionToString(ELinearConstraintMotion M)
	{
		switch (M)
		{
			case LCM_Free:    return TEXT("Free");
			case LCM_Limited: return TEXT("Limited");
			case LCM_Locked:  return TEXT("Locked");
			default:          return TEXT("Unknown");
		}
	}

	const TCHAR* PhysicsTypeToString(EPhysicsType T)
	{
		switch (T)
		{
			case PhysType_Default:   return TEXT("Default");
			case PhysType_Kinematic: return TEXT("Kinematic");
			case PhysType_Simulated: return TEXT("Simulated");
			default:                 return TEXT("Unknown");
		}
	}

	const TCHAR* CollisionResponseToString(EBodyCollisionResponse::Type R)
	{
		switch (R)
		{
			case EBodyCollisionResponse::BodyCollision_Enabled:       return TEXT("Enabled");
			case EBodyCollisionResponse::BodyCollision_Disabled:      return TEXT("Disabled");
			default:                                                  return TEXT("Unknown");
		}
	}

	bool SavePhysicsAsset(UPhysicsAsset* PhysAsset)
	{
		if (!PhysAsset) return false;
		PhysAsset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(PhysAsset, /*bOnlyIfIsDirty=*/false);
	}
}

// ============================================================================
// Lifecycle
// ============================================================================

FString UPhysicsAssetService::CreatePhysicsAsset(
	const FString& SkeletonAssetPath,
	const FString& OutputAssetPath,
	const FString& BindSkeletalMeshPath)
{
	if (OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("CreatePhysicsAsset: OutputAssetPath is required"));
		return FString();
	}

	FString PackagePath, AssetName;
	SplitObjectPath(OutputAssetPath, PackagePath, AssetName);

	const FString FullAssetPath = PackagePath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("CreatePhysicsAsset: asset already exists at '%s' - refusing to overwrite"), *FullAssetPath);
		return FString();
	}

	// Optionally resolve the skeletal mesh we will bind.
	USkeletalMesh* SkelMesh = nullptr;
	if (!BindSkeletalMeshPath.IsEmpty())
	{
		SkelMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(BindSkeletalMeshPath));
		if (!SkelMesh)
		{
			UE_LOG(LogRevvyPhysAsset, Warning,
				TEXT("CreatePhysicsAsset: BindSkeletalMeshPath '%s' did not resolve to a USkeletalMesh"),
				*BindSkeletalMeshPath);
		}
	}

	// Optional: pull preview mesh from skeleton if no explicit skeletal mesh.
	if (!SkelMesh && !SkeletonAssetPath.IsEmpty())
	{
		if (USkeleton* Skel = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonAssetPath)))
		{
			SkelMesh = Skel->GetPreviewMesh();
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "CreatePhysicsAsset", "Create Physics Asset"));

	// Bare package creation (no dialogs, deterministic).
	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("CreatePhysicsAsset: CreatePackage failed for '%s'"), *FullAssetPath);
		return FString();
	}
	Package->FullyLoad();

	UPhysicsAsset* PhysAsset = NewObject<UPhysicsAsset>(
		Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!PhysAsset)
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("CreatePhysicsAsset: NewObject<UPhysicsAsset> failed"));
		return FString();
	}
	PhysAsset->Modify();

	// Bind preview mesh + adopt PhysAsset on mesh if provided.
	if (SkelMesh)
	{
		PhysAsset->SetPreviewMesh(SkelMesh);
		SkelMesh->Modify();
		SkelMesh->SetPhysicsAsset(PhysAsset);
	}

	FAssetRegistryModule::AssetCreated(PhysAsset);
	PhysAsset->MarkPackageDirty();
	SavePhysicsAsset(PhysAsset);

	UE_LOG(LogRevvyPhysAsset, Log, TEXT("CreatePhysicsAsset: created '%s'"), *PhysAsset->GetPathName());
	return PhysAsset->GetPathName();
}

FString UPhysicsAssetService::AutoGenerateFromSkeletalMesh(
	const FString& SkelMeshAssetPath,
	const FString& OutputAssetPath,
	float MinBoneSize,
	float WeightThreshold,
	const FString& GeomTypeStr)
{
	if (SkelMeshAssetPath.IsEmpty() || OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("AutoGenerateFromSkeletalMesh: both SkelMeshAssetPath and OutputAssetPath are required"));
		return FString();
	}

	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SkelMeshAssetPath));
	if (!SkelMesh)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("AutoGenerateFromSkeletalMesh: failed to load SkeletalMesh at '%s'"), *SkelMeshAssetPath);
		return FString();
	}

	FString PackagePath, AssetName;
	SplitObjectPath(OutputAssetPath, PackagePath, AssetName);

	const FString FullAssetPath = PackagePath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("AutoGenerateFromSkeletalMesh: asset already exists at '%s' - refusing to overwrite"), *FullAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "AutoGenerateFromSkeletalMesh", "Auto Generate Physics Asset"));

	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AutoGenerateFromSkeletalMesh: CreatePackage failed for '%s'"), *FullAssetPath);
		return FString();
	}
	Package->FullyLoad();

	UPhysicsAsset* PhysAsset = NewObject<UPhysicsAsset>(
		Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!PhysAsset)
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AutoGenerateFromSkeletalMesh: NewObject<UPhysicsAsset> failed"));
		return FString();
	}
	PhysAsset->Modify();

	// Build params. NOTE: FPhysAssetCreateParams does NOT expose WeightThreshold or a
	// weighting-method enum on UE 5.8; incoming WeightThreshold selects EVW_AnyWeight
	// (below ~0.5) or EVW_DominantWeight (>= 0.5) as a best-effort mapping.
	FPhysAssetCreateParams Params;
	Params.MinBoneSize = MinBoneSize;
	Params.VertWeight  = (WeightThreshold < 0.5f) ? EVW_AnyWeight : EVW_DominantWeight;
	EPhysAssetFitGeomType ParsedGeom;
	if (ParseGeomType(GeomTypeStr, ParsedGeom))
	{
		Params.GeomType = ParsedGeom;
	}
	else if (!GeomTypeStr.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("AutoGenerateFromSkeletalMesh: unknown GeomType '%s' - defaulting to Sphyl"), *GeomTypeStr);
	}

	FText ErrorText;
	const bool bOk = FPhysicsAssetUtils::CreateFromSkeletalMesh(
		PhysAsset, SkelMesh, Params, ErrorText,
		/*bSetToMesh=*/true, /*bShowProgress=*/false);

	if (!bOk)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("AutoGenerateFromSkeletalMesh: CreateFromSkeletalMesh failed: %s"),
			*ErrorText.ToString());
		return FString();
	}

	FAssetRegistryModule::AssetCreated(PhysAsset);
	PhysAsset->MarkPackageDirty();
	SavePhysicsAsset(PhysAsset);

	UE_LOG(LogRevvyPhysAsset, Log, TEXT("AutoGenerateFromSkeletalMesh: created '%s' with %d bodies / %d constraints"),
		*PhysAsset->GetPathName(),
		PhysAsset->SkeletalBodySetups.Num(),
		PhysAsset->ConstraintSetup.Num());
	return PhysAsset->GetPathName();
}

// ============================================================================
// Body Management
// ============================================================================

FString UPhysicsAssetService::ListPhysicsBodies(const FString& PhysAssetPath)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("ListPhysicsBodies"));
	if (!PhysAsset)
	{
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> BodyArray;
	BodyArray.Reserve(PhysAsset->SkeletalBodySetups.Num());

	for (const TObjectPtr<USkeletalBodySetup>& BodyPtr : PhysAsset->SkeletalBodySetups)
	{
		USkeletalBodySetup* Body = BodyPtr.Get();
		if (!Body) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("bone_name"), Body->BoneName.ToString());
		Obj->SetNumberField(TEXT("num_sphere"),          Body->AggGeom.SphereElems.Num());
		Obj->SetNumberField(TEXT("num_box"),             Body->AggGeom.BoxElems.Num());
		Obj->SetNumberField(TEXT("num_sphyl"),           Body->AggGeom.SphylElems.Num());
		Obj->SetNumberField(TEXT("num_tapered_capsule"), Body->AggGeom.TaperedCapsuleElems.Num());

		const float Mass = Body->DefaultInstance.bOverrideMass
			? Body->DefaultInstance.GetMassOverride()
			: -1.0f; // -1 => not overridden
		Obj->SetNumberField(TEXT("mass"), Mass);
		Obj->SetStringField(TEXT("physics_type"),
			PhysicsTypeToString(Body->PhysicsType.GetValue()));
		Obj->SetStringField(TEXT("collision_response"),
			CollisionResponseToString(Body->CollisionReponse.GetValue()));

		BodyArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(BodyArray, Writer);
	return Out;
}

int32 UPhysicsAssetService::AddSphereBody(
	const FString& PhysAssetPath,
	const FString& BoneName,
	float Radius,
	FVector CenterOffset)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("AddSphereBody"));
	if (!PhysAsset) return -1;
	if (BoneName.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddSphereBody: BoneName is required"));
		return -1;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "AddSphereBody", "Add Sphere Body"));
	PhysAsset->Modify();

	const FName BoneFName(*BoneName);
	int32 BodyIndex = INDEX_NONE;
	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, BoneFName, &BodyIndex);
	if (!Body)
	{
		FPhysAssetCreateParams Params;
		BodyIndex = FPhysicsAssetUtils::CreateNewBody(PhysAsset, BoneFName, Params);
		if (BodyIndex == INDEX_NONE || !PhysAsset->SkeletalBodySetups.IsValidIndex(BodyIndex))
		{
			UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddSphereBody: CreateNewBody failed for '%s'"), *BoneName);
			return -1;
		}
		Body = Cast<USkeletalBodySetup>(PhysAsset->SkeletalBodySetups[BodyIndex]);
		if (Body) Body->BoneName = BoneFName;
	}
	if (!Body)
	{
		return -1;
	}

	Body->Modify();

	FKSphereElem SphereElem;
	SphereElem.Radius = Radius;
	SphereElem.Center = CenterOffset;
	Body->AggGeom.SphereElems.Add(SphereElem);

	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
	SavePhysicsAsset(PhysAsset);
	return BodyIndex;
}

int32 UPhysicsAssetService::AddSphylBody(
	const FString& PhysAssetPath,
	const FString& BoneName,
	float Radius,
	float Length,
	FVector CenterOffset,
	FRotator RotationOffset)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("AddSphylBody"));
	if (!PhysAsset) return -1;
	if (BoneName.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddSphylBody: BoneName is required"));
		return -1;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "AddSphylBody", "Add Sphyl Body"));
	PhysAsset->Modify();

	const FName BoneFName(*BoneName);
	int32 BodyIndex = INDEX_NONE;
	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, BoneFName, &BodyIndex);
	if (!Body)
	{
		FPhysAssetCreateParams Params;
		BodyIndex = FPhysicsAssetUtils::CreateNewBody(PhysAsset, BoneFName, Params);
		if (BodyIndex == INDEX_NONE || !PhysAsset->SkeletalBodySetups.IsValidIndex(BodyIndex))
		{
			UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddSphylBody: CreateNewBody failed for '%s'"), *BoneName);
			return -1;
		}
		Body = Cast<USkeletalBodySetup>(PhysAsset->SkeletalBodySetups[BodyIndex]);
		if (Body) Body->BoneName = BoneFName;
	}
	if (!Body)
	{
		return -1;
	}

	Body->Modify();

	FKSphylElem SphylElem;
	SphylElem.Radius   = Radius;
	SphylElem.Length   = Length;
	SphylElem.Center   = CenterOffset;
	SphylElem.Rotation = RotationOffset;
	Body->AggGeom.SphylElems.Add(SphylElem);

	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
	SavePhysicsAsset(PhysAsset);
	return BodyIndex;
}

int32 UPhysicsAssetService::AddBoxBody(
	const FString& PhysAssetPath,
	const FString& BoneName,
	FVector Extent,
	FVector CenterOffset,
	FRotator RotationOffset)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("AddBoxBody"));
	if (!PhysAsset) return -1;
	if (BoneName.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddBoxBody: BoneName is required"));
		return -1;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "AddBoxBody", "Add Box Body"));
	PhysAsset->Modify();

	const FName BoneFName(*BoneName);
	int32 BodyIndex = INDEX_NONE;
	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, BoneFName, &BodyIndex);
	if (!Body)
	{
		FPhysAssetCreateParams Params;
		BodyIndex = FPhysicsAssetUtils::CreateNewBody(PhysAsset, BoneFName, Params);
		if (BodyIndex == INDEX_NONE || !PhysAsset->SkeletalBodySetups.IsValidIndex(BodyIndex))
		{
			UE_LOG(LogRevvyPhysAsset, Warning, TEXT("AddBoxBody: CreateNewBody failed for '%s'"), *BoneName);
			return -1;
		}
		Body = Cast<USkeletalBodySetup>(PhysAsset->SkeletalBodySetups[BodyIndex]);
		if (Body) Body->BoneName = BoneFName;
	}
	if (!Body)
	{
		return -1;
	}

	Body->Modify();

	FKBoxElem BoxElem;
	BoxElem.X        = Extent.X;
	BoxElem.Y        = Extent.Y;
	BoxElem.Z        = Extent.Z;
	BoxElem.Center   = CenterOffset;
	BoxElem.Rotation = RotationOffset;
	Body->AggGeom.BoxElems.Add(BoxElem);

	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
	SavePhysicsAsset(PhysAsset);
	return BodyIndex;
}

bool UPhysicsAssetService::RemovePhysicsBody(const FString& PhysAssetPath, const FString& BoneName)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("RemovePhysicsBody"));
	if (!PhysAsset) return false;
	if (BoneName.IsEmpty())
	{
		UE_LOG(LogRevvyPhysAsset, Warning, TEXT("RemovePhysicsBody: BoneName is required"));
		return false;
	}

	int32 BodyIndex = INDEX_NONE;
	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, FName(*BoneName), &BodyIndex);
	if (!Body || BodyIndex == INDEX_NONE)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("RemovePhysicsBody: no body for bone '%s' in '%s'"), *BoneName, *PhysAssetPath);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "RemovePhysicsBody", "Remove Physics Body"));
	PhysAsset->Modify();
	Body->Modify();

	FPhysicsAssetUtils::DestroyBody(PhysAsset, BodyIndex);

	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
	SavePhysicsAsset(PhysAsset);
	return true;
}

// ============================================================================
// Property Edit
// ============================================================================

bool UPhysicsAssetService::SetPhysicsBodyMass(const FString& PhysAssetPath, const FString& BoneName, float Mass)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("SetPhysicsBodyMass"));
	if (!PhysAsset) return false;

	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, FName(*BoneName));
	if (!Body)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetPhysicsBodyMass: no body for bone '%s'"), *BoneName);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "SetPhysicsBodyMass", "Set Physics Body Mass"));
	PhysAsset->Modify();
	Body->Modify();

	Body->DefaultInstance.SetMassOverride(FMath::Max(Mass, 0.0f), true);

	SavePhysicsAsset(PhysAsset);
	return true;
}

bool UPhysicsAssetService::SetPhysicsBodyCollisionType(
	const FString& PhysAssetPath,
	const FString& BoneName,
	const FString& CollisionTypeStr)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("SetPhysicsBodyCollisionType"));
	if (!PhysAsset) return false;

	USkeletalBodySetup* Body = FindBodyByBoneName(PhysAsset, FName(*BoneName));
	if (!Body)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetPhysicsBodyCollisionType: no body for bone '%s'"), *BoneName);
		return false;
	}

	// Parse to ECollisionEnabled + the wider EBodyCollisionResponse flag.
	ECollisionEnabled::Type            Enabled  = ECollisionEnabled::QueryAndPhysics;
	EBodyCollisionResponse::Type       Response = EBodyCollisionResponse::BodyCollision_Enabled;
	const FString L = CollisionTypeStr.ToLower();
	if      (L == TEXT("allqueriesandphysics")) { Enabled = ECollisionEnabled::QueryAndPhysics; Response = EBodyCollisionResponse::BodyCollision_Enabled; }
	else if (L == TEXT("onlyphysics"))          { Enabled = ECollisionEnabled::PhysicsOnly;     Response = EBodyCollisionResponse::BodyCollision_Enabled; }
	else if (L == TEXT("onlyqueries"))          { Enabled = ECollisionEnabled::QueryOnly;       Response = EBodyCollisionResponse::BodyCollision_Enabled; }
	else if (L == TEXT("nocollision"))          { Enabled = ECollisionEnabled::NoCollision;     Response = EBodyCollisionResponse::BodyCollision_Disabled; }
	else
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetPhysicsBodyCollisionType: unknown CollisionType '%s'"), *CollisionTypeStr);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "SetPhysicsBodyCollisionType", "Set Physics Body Collision Type"));
	PhysAsset->Modify();
	Body->Modify();

	Body->DefaultInstance.SetCollisionEnabled(Enabled);
	Body->CollisionReponse = Response; // engine field name preserves the historical typo

	SavePhysicsAsset(PhysAsset);
	return true;
}

// ============================================================================
// Constraints
// ============================================================================

FString UPhysicsAssetService::ListPhysicsConstraints(const FString& PhysAssetPath)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("ListPhysicsConstraints"));
	if (!PhysAsset)
	{
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> ConArray;
	ConArray.Reserve(PhysAsset->ConstraintSetup.Num());

	for (UPhysicsConstraintTemplate* Con : PhysAsset->ConstraintSetup)
	{
		if (!Con) continue;
		FConstraintInstance& CI = Con->DefaultInstance;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("child_bone"),  CI.ConstraintBone1.ToString());
		Obj->SetStringField(TEXT("parent_bone"), CI.ConstraintBone2.ToString());
		Obj->SetStringField(TEXT("joint_name"),  CI.JointName.ToString());

		Obj->SetNumberField(TEXT("swing1_deg"), CI.ProfileInstance.ConeLimit.Swing1LimitDegrees);
		Obj->SetNumberField(TEXT("swing2_deg"), CI.ProfileInstance.ConeLimit.Swing2LimitDegrees);
		Obj->SetNumberField(TEXT("twist_deg"),  CI.ProfileInstance.TwistLimit.TwistLimitDegrees);

		Obj->SetStringField(TEXT("motion_swing1"),
			AngularMotionToString(static_cast<EAngularConstraintMotion>(CI.ProfileInstance.ConeLimit.Swing1Motion.GetValue())));
		Obj->SetStringField(TEXT("motion_swing2"),
			AngularMotionToString(static_cast<EAngularConstraintMotion>(CI.ProfileInstance.ConeLimit.Swing2Motion.GetValue())));
		Obj->SetStringField(TEXT("motion_twist"),
			AngularMotionToString(static_cast<EAngularConstraintMotion>(CI.ProfileInstance.TwistLimit.TwistMotion.GetValue())));

		Obj->SetStringField(TEXT("motion_linear_x"),
			LinearMotionToString(static_cast<ELinearConstraintMotion>(CI.ProfileInstance.LinearLimit.XMotion.GetValue())));
		Obj->SetStringField(TEXT("motion_linear_y"),
			LinearMotionToString(static_cast<ELinearConstraintMotion>(CI.ProfileInstance.LinearLimit.YMotion.GetValue())));
		Obj->SetStringField(TEXT("motion_linear_z"),
			LinearMotionToString(static_cast<ELinearConstraintMotion>(CI.ProfileInstance.LinearLimit.ZMotion.GetValue())));

		ConArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(ConArray, Writer);
	return Out;
}

bool UPhysicsAssetService::SetConstraintAngularLimits(
	const FString& PhysAssetPath,
	const FString& ChildBoneName,
	float Swing1Deg,
	float Swing2Deg,
	float TwistDeg)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("SetConstraintAngularLimits"));
	if (!PhysAsset) return false;

	UPhysicsConstraintTemplate* Con = FindConstraintByChildBone(PhysAsset, FName(*ChildBoneName));
	if (!Con)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetConstraintAngularLimits: no constraint for child bone '%s'"), *ChildBoneName);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "SetConstraintAngularLimits", "Set Constraint Angular Limits"));
	PhysAsset->Modify();
	Con->Modify();

	Con->DefaultInstance.SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, Swing1Deg);
	Con->DefaultInstance.SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, Swing2Deg);
	Con->DefaultInstance.SetAngularTwistLimit (EAngularConstraintMotion::ACM_Limited, TwistDeg);
	Con->UpdateProfileInstance();

	SavePhysicsAsset(PhysAsset);
	return true;
}

bool UPhysicsAssetService::SetConstraintMotionType(
	const FString& PhysAssetPath,
	const FString& ChildBoneName,
	const FString& AxisStr,
	const FString& MotionStr)
{
	UPhysicsAsset* PhysAsset = LoadPhysicsAssetChecked(PhysAssetPath, TEXT("SetConstraintMotionType"));
	if (!PhysAsset) return false;

	UPhysicsConstraintTemplate* Con = FindConstraintByChildBone(PhysAsset, FName(*ChildBoneName));
	if (!Con)
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetConstraintMotionType: no constraint for child bone '%s'"), *ChildBoneName);
		return false;
	}

	const FString Axis = AxisStr.ToLower();
	FConstraintInstance& CI = Con->DefaultInstance;

	FScopedTransaction Tx(NSLOCTEXT("PhysicsAssetService", "SetConstraintMotionType", "Set Constraint Motion Type"));
	PhysAsset->Modify();
	Con->Modify();

	bool bHandled = false;

	if (Axis == TEXT("swing1") || Axis == TEXT("swing2") || Axis == TEXT("twist"))
	{
		EAngularConstraintMotion Motion;
		if (!ParseAngularMotion(MotionStr, Motion))
		{
			UE_LOG(LogRevvyPhysAsset, Warning,
				TEXT("SetConstraintMotionType: unknown angular motion '%s' (expected Free|Limited|Locked)"),
				*MotionStr);
			return false;
		}

		if (Axis == TEXT("swing1"))
		{
			const float CurLimit = CI.ProfileInstance.ConeLimit.Swing1LimitDegrees;
			CI.SetAngularSwing1Limit(Motion, CurLimit);
			bHandled = true;
		}
		else if (Axis == TEXT("swing2"))
		{
			const float CurLimit = CI.ProfileInstance.ConeLimit.Swing2LimitDegrees;
			CI.SetAngularSwing2Limit(Motion, CurLimit);
			bHandled = true;
		}
		else // twist
		{
			const float CurLimit = CI.ProfileInstance.TwistLimit.TwistLimitDegrees;
			CI.SetAngularTwistLimit(Motion, CurLimit);
			bHandled = true;
		}
	}
	else if (Axis == TEXT("linearx") || Axis == TEXT("lineary") || Axis == TEXT("linearz") ||
			 Axis == TEXT("linear_x") || Axis == TEXT("linear_y") || Axis == TEXT("linear_z"))
	{
		ELinearConstraintMotion Motion;
		if (!ParseLinearMotion(MotionStr, Motion))
		{
			UE_LOG(LogRevvyPhysAsset, Warning,
				TEXT("SetConstraintMotionType: unknown linear motion '%s' (expected Free|Limited|Locked)"),
				*MotionStr);
			return false;
		}

		const float CurLimit = CI.ProfileInstance.LinearLimit.Limit;
		if (Axis == TEXT("linearx") || Axis == TEXT("linear_x"))
		{
			CI.SetLinearXLimit(Motion, CurLimit);
			bHandled = true;
		}
		else if (Axis == TEXT("lineary") || Axis == TEXT("linear_y"))
		{
			CI.SetLinearYLimit(Motion, CurLimit);
			bHandled = true;
		}
		else
		{
			CI.SetLinearZLimit(Motion, CurLimit);
			bHandled = true;
		}
	}
	else
	{
		UE_LOG(LogRevvyPhysAsset, Warning,
			TEXT("SetConstraintMotionType: unknown axis '%s' (expected Swing1|Swing2|Twist|LinearX|LinearY|LinearZ)"),
			*AxisStr);
		return false;
	}

	if (bHandled)
	{
		Con->UpdateProfileInstance();
		SavePhysicsAsset(PhysAsset);
	}
	return bHandled;
}
