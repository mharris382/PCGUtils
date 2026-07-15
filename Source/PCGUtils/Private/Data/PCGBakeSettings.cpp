#include "Data/PCGBakeSettings.h"


bool FPCGUtilsBakeSettings::IsValidSavePath() const
{
	return !BakedAssetSaveName.IsEmpty();
}

FSoftObjectPath FPCGUtilsBakeSettings::GetBakedAssetSoftPath() const
{
	return BakedAssetSavePath.Path + "\\" + GetBakeAssetSaveName() ;
}
