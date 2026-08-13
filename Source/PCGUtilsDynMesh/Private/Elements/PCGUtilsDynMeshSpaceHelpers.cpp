#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"

#include "Data/PCGDynamicMeshData.h"
#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSpaceHelpers"

namespace PCGUtilsDynMeshSpaceHelpers
{
	FTransform ResolveMeshActorTransform(FPCGContext* Context, const UPCGDynamicMeshData* MeshData, bool bConvertToLocalSpace)
	{
		if (!bConvertToLocalSpace)
		{
			return FTransform::Identity;
		}

		if (const AActor* TargetActor = Context->GetTargetActor(MeshData))
		{
			return TargetActor->GetActorTransform();
		}

		PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
			"Could not resolve a PCG target actor for coordinate-space conversion; treating the input as already being in the Dynamic Mesh's coordinate space."), Context);
		return FTransform::Identity;
	}
}

#undef LOCTEXT_NAMESPACE
