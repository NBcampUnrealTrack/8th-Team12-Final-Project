// Copyright Recursoft LLC. All Rights Reserved.

#include "Assets/SMInstalledContentAsset.h"

void USMInstalledContentAsset::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);
	Ar << PAKFileHash;
}
