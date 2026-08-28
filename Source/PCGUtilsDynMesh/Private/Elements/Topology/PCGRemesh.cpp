#include "Elements/Topology/PCGRemesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGRemeshElement"

namespace
{
	const FName MeshPin = TEXT("Mesh");

	/** Finds a named vertex weight-map layer on Mesh, or INDEX_NONE if no layer with that name exists. */
	int32 FindWeightMapLayerIndex(const UE::Geometry::FDynamicMesh3& Mesh, FName Name)
	{
		if (!Mesh.HasAttributes())
		{
			return INDEX_NONE;
		}
		for (int32 LayerIndex = 0; LayerIndex < Mesh.Attributes()->NumWeightLayers(); ++LayerIndex)
		{
			const UE::Geometry::FDynamicMeshWeightAttribute* Layer = Mesh.Attributes()->GetWeightLayer(LayerIndex);
			if (Layer && Layer->GetName() == Name)
			{
				return LayerIndex;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Resolves a valid Adaptive-remesh weight-map handle for Mesh. ApplyAdaptiveRemesh requires a *valid* handle
	 * even when no density modulation is desired (an invalid handle is treated as an error, not "no weight map"),
	 * so when Settings->bUseAdaptiveWeightMap is off, or the named layer can't be found, a temporary neutral
	 * (constant 1.0) layer is synthesized instead - with Relative Density's default of 0, a constant weight map
	 * has no effect on the result. bOutIsTemporary tells the caller to remove that synthesized layer afterward.
	 */
	FGeometryScriptWeightMapHandle ResolveAdaptiveWeightMapHandle(
		UDynamicMesh* Mesh, const FPCGUtilsDynMeshRemeshOperation* Settings, bool& bOutIsTemporary,
		FPCGContext* Context)
	{
		using namespace UE::Geometry;

		bOutIsTemporary = false;
		FGeometryScriptWeightMapHandle Handle;

		if (Settings->bUseAdaptiveWeightMap)
		{
			Mesh->ProcessMesh([&Handle, Settings](const FDynamicMesh3& M)
			{
				Handle.WeightMapAttributeLayerIndex = FindWeightMapLayerIndex(M, Settings->AdaptiveWeightMapAttributeName);
			});
			if (!Handle.IsValid())
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("MissingAdaptiveWeightMap", "Remesh (Adaptive) could not find vertex weight map '{0}'; using a neutral weight map instead."),
					FText::FromName(Settings->AdaptiveWeightMapAttributeName)), Context);
			}
		}

		if (!Handle.IsValid())
		{
			Mesh->EditMesh([&Handle](FDynamicMesh3& M)
			{
				M.EnableAttributes();
				const int32 NewIndex = M.Attributes()->NumWeightLayers();
				M.Attributes()->SetNumWeightLayers(NewIndex + 1);
				FDynamicMeshWeightAttribute* Layer = M.Attributes()->GetWeightLayer(NewIndex);
				for (const int32 VertexID : M.VertexIndicesItr())
				{
					Layer->SetScalarValue(VertexID, 1.0f);
				}
				Handle.WeightMapAttributeLayerIndex = NewIndex;
			});
			bOutIsTemporary = true;
		}

		return Handle;
	}

	void RemoveTemporaryWeightMap(UDynamicMesh* Mesh, int32 LayerIndex)
	{
		Mesh->EditMesh([LayerIndex](UE::Geometry::FDynamicMesh3& M)
		{
			if (M.HasAttributes() && LayerIndex >= 0 && LayerIndex < M.Attributes()->NumWeightLayers())
			{
				M.Attributes()->RemoveWeightLayer(LayerIndex);
			}
		});
	}

	/**
	 * Applies the user's chosen remesh mode to Mesh via Geometry Script. When bForceFixedSelectionBoundary is
	 * true (Mesh input was a Mesh Selection, so Mesh is the extracted region), the boundary constraint is forced
	 * to Fixed and Auto Compact is deferred to the caller - see RemeshOne - without ever mutating the user's own
	 * serialized RemeshOptions.
	 */
	void ApplyRemeshOperation(UDynamicMesh* Mesh, const FPCGUtilsDynMeshRemeshOperation* Settings,
		bool bForceFixedSelectionBoundary, FPCGContext* Context)
	{
		FGeometryScriptRemeshOptions EffectiveOptions = Settings->RemeshOptions;
		if (bForceFixedSelectionBoundary)
		{
			EffectiveOptions.MeshBoundaryConstraint = EGeometryScriptRemeshEdgeConstraintType::Fixed;
			EffectiveOptions.bAutoCompact = false;
		}

		if (Settings->Mode == EPCGRemeshMode::Uniform)
		{
			UGeometryScriptLibrary_RemeshingFunctions::ApplyUniformRemesh(Mesh, EffectiveOptions, Settings->UniformOptions);
		}
		else
		{
			bool bIsTemporaryWeightMap = false;
			const FGeometryScriptWeightMapHandle Handle = ResolveAdaptiveWeightMapHandle(Mesh, Settings, bIsTemporaryWeightMap, Context);
			UGeometryScriptLibrary_RemeshingFunctions::ApplyAdaptiveRemesh(Mesh, EffectiveOptions, Settings->AdaptiveOptions, Handle);
			if (bIsTemporaryWeightMap)
			{
				RemoveTemporaryWeightMap(Mesh, Handle.WeightMapAttributeLayerIndex);
			}
		}
	}

}

#if WITH_EDITOR
FText UPCGRemeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Remesh");
}

FText UPCGRemeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies Geometry Script Uniform or Adaptive remeshing to Dynamic Mesh data. If the Mesh input is a Mesh "
		"Selection, only the selected region is remeshed - extracted, remeshed with a fixed outer boundary, and "
		"welded back into the untouched remainder of the source mesh.");
}
#endif

FName UPCGRemeshSettings::GetMainInputPinLabel() const
{
	return MeshPin;
}

FPCGElementPtr UPCGRemeshSettings::CreateElement() const
{
	return MakeShared<FPCGRemeshElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGRemeshSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshRemeshOperation> Operation = MakeShared<FPCGUtilsDynMeshRemeshOperation>();
	Operation->Mode = Mode;
	Operation->RemeshOptions = RemeshOptions;
	Operation->UniformOptions = UniformOptions;
	Operation->AdaptiveOptions = AdaptiveOptions;
	Operation->bUseAdaptiveWeightMap = bUseAdaptiveWeightMap;
	Operation->AdaptiveWeightMapAttributeName = AdaptiveWeightMapAttributeName;
	return Operation;
}

bool FPCGUtilsDynMeshRemeshOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	// Remeshing replaces the triangulation outright, so no selection into the old topology survives it.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::Region);
	if (!Handle.IsValid())
	{
		return false;
	}

	const bool bIsSelectionRegion = Handle.IsSelection() && !Handle.IsEmptySelectionNoOp();

	if (!Handle.IsEmptySelectionNoOp())
	{
		ApplyRemeshOperation(Handle.GetTargetMesh(), this, bIsSelectionRegion, Invocation.Context);
	}

	FPCGUtilsMeshTargetFunctions::RestoreRegion(Handle);

	// AutoCompact is honored once, here, on the final merged mesh rather than during the region's own remesh
	// pass (which is forced to skip it in ApplyRemeshOperation) - compacting the temporary region mid-pass
	// would renumber the boundary-vertex IDs the reinsertion/weld step depends on.
	if (bIsSelectionRegion && RemeshOptions.bAutoCompact)
	{
		Handle.GetTargetMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& M) { M.CompactInPlace(); });
	}

	// Nothing to emit: CreateTargetInPlace adopted Invocation.MeshData's mesh as the handle's base.
	return true;
}

#undef LOCTEXT_NAMESPACE
