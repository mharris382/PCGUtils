// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshDecimate.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/PCGUtilsDynMeshDecimate.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshDecimate"

#if WITH_EDITOR
FText UPCGDynMeshDecimateSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Decimate");
}

FText UPCGDynMeshDecimateSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Blender's Decimate modifier. Collapse reduces to a fraction of the triangles; Un-Subdivide reverses grid "
		"subdivision; Planar merges faces that are flat within an angle limit and retriangulates them. A DynMesh "
		"Selection or Selector limits which triangles change.");
}

FString UPCGDynMeshDecimateSettings::GetAdditionalTitleInformation() const
{
	switch (Mode)
	{
	case EPCGUtilsDynMeshDecimateMode::Collapse: return FString::Printf(TEXT("Collapse %s"), *FString::SanitizeFloat(Ratio, 0));
	case EPCGUtilsDynMeshDecimateMode::UnSubdivide: return FString::Printf(TEXT("Un-Subdivide x%d"), Iterations);
	case EPCGUtilsDynMeshDecimateMode::Planar: return FString::Printf(TEXT("Planar %s deg"), *FString::SanitizeFloat(AngleLimit, 0));
	default: return FString();
	}
}
#endif

bool UPCGDynMeshDecimateSettings::GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const
{
	OutElementType = UE::Geometry::EGeometryElementType::Face;
	return true;
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshDecimateSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshDecimateOperation> Operation = MakeShared<FPCGUtilsDynMeshDecimateOperation>();
	Operation->Mode = Mode;
	Operation->Ratio = Ratio;
	Operation->bUseWeightMap = bUseWeightMap;
	Operation->WeightMapName = WeightMapName;
	Operation->WeightFactor = WeightFactor;
	Operation->CollapseOptions = CollapseOptions;
	Operation->Iterations = Iterations;
	Operation->AngleLimit = AngleLimit;
	Operation->bAllBoundaries = bAllBoundaries;
	Operation->bDelimitMaterial = bDelimitMaterial;
	Operation->bDelimitPolygroup = bDelimitPolygroup;
	Operation->bDelimitUVSeams = bDelimitUVSeams;
	Operation->bDelimitNormalSeams = bDelimitNormalSeams;
	return Operation;
}

FPCGElementPtr UPCGDynMeshDecimateSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshDecimateElement>();
}

namespace PCGUtilsDynMeshDecimateNode
{
	int32 FindWeightLayer(const UE::Geometry::FDynamicMesh3& Mesh, FName Name)
	{
		if (!Mesh.HasAttributes())
		{
			return INDEX_NONE;
		}
		for (int32 Layer = 0; Layer < Mesh.Attributes()->NumWeightLayers(); ++Layer)
		{
			if (Mesh.Attributes()->GetWeightLayer(Layer)->GetName() == Name)
			{
				return Layer;
			}
		}
		return INDEX_NONE;
	}
}

bool FPCGUtilsDynMeshDecimateOperation::Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	check(Invocation.MeshData);
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

	if (Mode == EPCGUtilsDynMeshDecimateMode::Collapse)
	{
		if (Ratio < 0.0f || Ratio > 1.0f)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadRatio",
				"DynMesh | Decimate: Ratio is {0}; it must be between 0 and 1."), FText::AsNumber(Ratio)), Invocation.Context);
			return false;
		}

		// Geometry Script's simplifier is whole-mesh only: work on the selected region and weld it back.
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
			Invocation, EPCGUtilsMeshTargetPreparation::Region);
		if (!Handle.IsValid())
		{
			return false;
		}
		const bool bSelectionRegion = Handle.IsSelection() && !Handle.IsEmptySelectionNoOp();
		if (!Handle.IsEmptySelectionNoOp() && Ratio < 1.0f)
		{
			UDynamicMesh* Target = Handle.GetTargetMesh();
			const int32 TriangleCount = Target->GetTriangleCount();
			FGeometryScriptSimplifyMeshOptions Options = CollapseOptions;
			if (bUseWeightMap)
			{
				const int32 Layer = PCGUtilsDynMeshDecimateNode::FindWeightLayer(Target->GetMeshRef(), WeightMapName);
				if (Layer == INDEX_NONE)
				{
					PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MissingWeightMap",
						"DynMesh | Decimate: Weight Map Name is '{0}', but the mesh has no vertex weight map with that name."),
						FText::FromName(WeightMapName)), Invocation.Context);
					return false;
				}
				Options.QuadricErrorWeightMap.Handle.WeightMapAttributeLayerIndex = Layer;
				Options.QuadricErrorWeightMap.RelativeDensity = WeightFactor;
			}
			// Region restoration needs the pre-collapse boundary IDs, so compact only after the weld.
			if (bSelectionRegion)
			{
				Options.bAutoCompact = false;
			}
			const int32 Target_Count = FMath::Max(1, FMath::RoundToInt32(Ratio * TriangleCount));
			UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(Target, Target_Count, Options);
		}
		if (!FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle))
		{
			return false;
		}
		if (bSelectionRegion && CollapseOptions.bAutoCompact)
		{
			Handle.GetTargetMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh) { Mesh.CompactInPlace(); });
		}
		return true;
	}

	// Planar and Un-Subdivide are selection-aware themselves: unselected triangles are locked.
	TSet<int32> Selected;
	const TSet<int32>* SelectedPtr = nullptr;
	if (Invocation.SelectionData)
	{
		for (const uint64 EncodedID : Invocation.SelectionData->GetSelection().Selection)
		{
			Selected.Add(static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID));
		}
		if (Selected.IsEmpty())
		{
			return true;
		}
		SelectedPtr = &Selected;
	}

	if (Mode == EPCGUtilsDynMeshDecimateMode::UnSubdivide)
	{
		if (Iterations <= 0)
		{
			return true;
		}
		Invocation.MeshData->GetMutableDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
		{
			PCGUtilsDynMeshDecimate::Unsubdivide(Mesh, Iterations, SelectedPtr);
		});
		return true;
	}

	if (AngleLimit < 0.0f || AngleLimit > 180.0f)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadAngle",
			"DynMesh | Decimate: Angle Limit is {0} degrees; it must be between 0 and 180."), FText::AsNumber(AngleLimit)), Invocation.Context);
		return false;
	}
	PCGUtilsDynMeshDecimate::FPlanarOptions Options;
	Options.AngleLimitDegrees = AngleLimit;
	Options.bAllBoundaries = bAllBoundaries;
	Options.bDelimitMaterial = bDelimitMaterial;
	Options.bDelimitPolygroup = bDelimitPolygroup;
	Options.bDelimitUVSeams = bDelimitUVSeams;
	Options.bDelimitNormalSeams = bDelimitNormalSeams;
	Invocation.MeshData->GetMutableDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		PCGUtilsDynMeshDecimate::DissolvePlanar(Mesh, Options, SelectedPtr);
	});
	return true;
}

#undef LOCTEXT_NAMESPACE
