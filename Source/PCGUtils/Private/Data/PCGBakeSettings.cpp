#include "Data/PCGBakeSettings.h"


bool FPCGUtilsBakeSettings::IsValidSavePath() const
{
	return !BakedAssetSaveName.IsEmpty();
}

FSoftObjectPath FPCGUtilsBakeSettings::GetBakedAssetSoftPath() const
{
	return BakedAssetSavePath.Path + "\\" + GetBakeAssetSaveName() ;
}

FSoftObjectPath FPCGUtilsBakeSettings::GetBakedAssetObjectPath(const FString& NameSuffix) const
{
	FString Directory = BakedAssetSavePath.Path.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	while (Directory.EndsWith(TEXT("/")))
	{
		Directory.LeftChopInline(1);
	}
	if (BakedAssetSaveName.IsEmpty() || Directory.IsEmpty())
	{
		return FSoftObjectPath();
	}
	if (!Directory.StartsWith(TEXT("/")))
	{
		Directory = TEXT("/") + Directory;
	}

	const FString AssetName = BakedAssetSaveName + NameSuffix;
	return FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"), *Directory, *AssetName, *AssetName));
}
