// Copyright Max Harris

#include "Elements/PCGVoxelSettingsBase.h"

#include "Details/PCGUtilsInputShorthands.h"

#if WITH_EDITOR
TArray<FPCGSettingsOverridableParam> UPCGVoxelSettingsBase::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif
