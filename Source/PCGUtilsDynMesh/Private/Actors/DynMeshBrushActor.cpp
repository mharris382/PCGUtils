// Copyright Max Harris

#include "Actors/DynMeshBrushActor.h"

ADynMeshBrushActor::ADynMeshBrushActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsEditorOnlyActor = true;
}

EPCGUtilsDynMeshBrushType ADynMeshBrushActor::GetDynMeshBrushType_Implementation() const
{
	return BrushType;
}

FDynMeshBrushSettings ADynMeshBrushActor::GetDynMeshBrushSettings_Implementation() const
{
	return BrushSettings;
}

UDynamicMesh* ADynMeshBrushActor::RetrieveDynamicMesh_Implementation() const
{
	return nullptr;
}
