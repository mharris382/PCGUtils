// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectContact.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectContact"

bool UPCGGeometryCollectionSelectContactFactoryData::TransformSelection(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	TArray<int32>& InOutBones) const
{
	TArray<int32> Neighbors;
	if (!PCGUtilsGeometryCollectionHelpers::GatherContactNeighbors(
		InEvaluationContext.Collection, InOutBones, bIncludeNeighborsInParentLevels, Iterations, Neighbors))
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoProximity",
				"Select Contact could not determine which pieces touch, so no neighbours were added. The "
				"collection may have no geometry left."), InContext);
		InOutBones.Reset();
		return true;
	}

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Select Contact: %d bone(s) -> %d neighbour(s) over %d step(s)"),
		InOutBones.Num(), Neighbors.Num(), FMath::Max(1, Iterations));

	InOutBones = MoveTemp(Neighbors);
	return true;
}

void UPCGGeometryCollectionSelectContactFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	bool bLocalIncludeParentLevels = bIncludeNeighborsInParentLevels;
	int32 LocalIterations = Iterations;
	Ar << bLocalIncludeParentLevels;
	Ar << LocalIterations;
}

UPCGGeometryCollectionSelectContactSettings::UPCGGeometryCollectionSelectContactSettings()
{
	// Fracture Mode's Contact button grows the selection rather than replacing it, and that is what anyone
	// reaching for this node means. The other decorators replace, matching their own Fracture Mode buttons.
	bIncludeOriginal = true;
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectContactSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Select | Contact");
}

FText UPCGGeometryCollectionSelectContactSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Grows a bone selection to the pieces touching it, using the engine's precise proximity - shared "
		"vertices, or touching coplanar opposite-facing triangles, which is what a fracture cut produces. This "
		"is the expensive selector: proximity is recomputed from the geometry on every execution. For spreading "
		"with rules of your own, emit the adjacency graph from GC Bones To Points and flood fill it with PCGEx.");
}

FString UPCGGeometryCollectionSelectContactSettings::GetAdditionalTitleInformation() const
{
	return Iterations > 1 ? FString::Printf(TEXT("%d steps"), Iterations) : FString();
}
#endif

UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData*
UPCGGeometryCollectionSelectContactSettings::CreateDecoratorFactory(FPCGContext* InContext) const
{
	UPCGGeometryCollectionSelectContactFactoryData* Factory =
		FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectContactFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->bIncludeNeighborsInParentLevels = bIncludeNeighborsInParentLevels;
	Factory->Iterations = FMath::Max(1, Iterations);
	return Factory;
}

#undef LOCTEXT_NAMESPACE
