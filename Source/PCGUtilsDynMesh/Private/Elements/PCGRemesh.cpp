#include "Elements/PCGRemesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Materials/MaterialInterface.h"
#include "Operations/MeshRegionOperator.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGRemeshElement"

namespace
{
	const FName SelectionPin = TEXT("Selection");

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
		UDynamicMesh* Mesh, const UPCGRemeshSettings* Settings, bool& bOutIsTemporary, FPCGContext* Context)
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
	 * true (Mesh Selection mode remeshing the extracted region), the boundary constraint is forced to Fixed and
	 * Auto Compact is deferred - see the "Selection Boundary Must Be Fixed" and "Auto Compact" notes on the
	 * caller - without ever mutating the user's own serialized RemeshOptions.
	 */
	void ApplyRemeshOperation(UDynamicMesh* Mesh, const UPCGRemeshSettings* Settings, bool bForceFixedSelectionBoundary, FPCGContext* Context)
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

	TArray<UMaterialInterface*> CopyMaterials(const TArray<TObjectPtr<UMaterialInterface>>& Materials)
	{
		TArray<UMaterialInterface*> Result;
		Result.Reserve(Materials.Num());
		for (UMaterialInterface* Material : Materials)
		{
			Result.Add(Material);
		}
		return Result;
	}

	void EmitOutput(FPCGContext* Context, const FPCGTaggedData& Input, UDynamicMesh* WorkingMesh, const TArray<UMaterialInterface*>& Materials)
	{
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(WorkingMesh, /*bCanTakeOwnership=*/true, Materials);
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	void RemeshFullMesh(FPCGContext* Context, const UPCGRemeshSettings* Settings, const FPCGTaggedData& Input)
	{
		const UPCGDynamicMeshData* InputData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* SourceObject = InputData ? InputData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Remesh skipped an invalid Dynamic Mesh input."), Context);
			return;
		}
		if (SourceMesh->TriangleCount() == 0)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh", "Remesh skipped an empty Dynamic Mesh input."), Context);
			return;
		}

		const TArray<UMaterialInterface*> Materials = CopyMaterials(InputData->GetMaterials());

		UDynamicMesh* WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		WorkingMesh->SetMesh(*SourceMesh);

		ApplyRemeshOperation(WorkingMesh, Settings, /*bForceFixedSelectionBoundary=*/false, Context);

		EmitOutput(Context, Input, WorkingMesh, Materials);
	}

	void RemeshSelection(FPCGContext* Context, const UPCGRemeshSettings* Settings, const FPCGTaggedData& Input)
	{
		using namespace UE::Geometry;

		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelection", "Remesh skipped Mesh Selection data with no valid source mesh."), Context);
			return;
		}
		if (SourceMesh->TriangleCount() == 0)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("EmptySourceMesh", "Remesh skipped a Mesh Selection whose source mesh is empty."), Context);
			return;
		}

		FGeometryScriptMeshSelection Selection;
		Selection.SetSelection(SelectionData->GetSelection());

		TArray<int32> SelectedTriangles;
		Selection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);

		const TArray<UMaterialInterface*> Materials = CopyMaterials(SourceData->GetMaterials());

		UDynamicMesh* WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		WorkingMesh->SetMesh(*SourceMesh);

		if (SelectedTriangles.IsEmpty())
		{
			// A selection that resolves to no triangles is a legitimate no-op (eg an upstream filter found
			// nothing this run) - pass the source mesh through unchanged without warning.
			EmitOutput(Context, Input, WorkingMesh, Materials);
			return;
		}

		bool bReinsertSucceeded = false;
		WorkingMesh->EditMesh([Context, Settings, &SelectedTriangles, &bReinsertSucceeded](FDynamicMesh3& EditMesh)
		{
			// Extracts the selected triangles into a standalone submesh (preserving normals/UVs/colors/material
			// IDs/PolyGroups via FDynamicMeshEditor::AppendTriangles), and records which submesh vertices
			// correspond to the boundary between the selected and unselected mesh - this is exactly the
			// engine-provided mechanism the "Selection Boundary Must Be Fixed" and "Boundary Mapping"
			// requirements call for, rather than a hand-rolled position/ID tracking scheme.
			FMeshRegionOperator RegionOp(&EditMesh, SelectedTriangles);
			FDynamicMesh3& Submesh = RegionOp.Region.GetSubmesh();

			// Extraction only allocates a matching-*named* weight layer on the submesh (with default/zero
			// values) - it does not copy per-vertex weight *values*, unlike overlay-based UVs/normals/colors.
			// Copy the values across explicitly using the submesh's own vertex mapping before remeshing.
			if (Settings->Mode == EPCGRemeshMode::Adaptive && Settings->bUseAdaptiveWeightMap)
			{
				const int32 BaseLayerIndex = FindWeightMapLayerIndex(EditMesh, Settings->AdaptiveWeightMapAttributeName);
				const int32 SubLayerIndex = FindWeightMapLayerIndex(Submesh, Settings->AdaptiveWeightMapAttributeName);
				if (BaseLayerIndex != INDEX_NONE && SubLayerIndex != INDEX_NONE)
				{
					const FDynamicMeshWeightAttribute* BaseLayer = EditMesh.Attributes()->GetWeightLayer(BaseLayerIndex);
					FDynamicMeshWeightAttribute* SubLayer = Submesh.Attributes()->GetWeightLayer(SubLayerIndex);
					for (const int32 SubVertexID : Submesh.VertexIndicesItr())
					{
						const int32 BaseVertexID = RegionOp.Region.MapVertexToBaseMesh(SubVertexID);
						float Value = 0.0f;
						if (BaseVertexID != INDEX_NONE)
						{
							BaseLayer->GetValue(BaseVertexID, &Value);
						}
						SubLayer->SetScalarValue(SubVertexID, Value);
					}
				}
			}

			// Remesh the extracted region only, via a transient UDynamicMesh wrapper (Geometry Script's remesh
			// functions require one). Auto Compact is forced off for this pass (see ApplyRemeshOperation) so the
			// submesh's boundary-vertex IDs - captured above by FMeshRegionOperator - stay valid for reinsertion;
			// the user's own Auto Compact setting is honored once, below, on the final merged mesh instead.
			UDynamicMesh* TempMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
			TempMesh->SetMesh(MoveTemp(Submesh));
			ApplyRemeshOperation(TempMesh, Settings, /*bForceFixedSelectionBoundary=*/true, Context);
			Submesh = MoveTemp(*TempMesh->GetMeshPtr());

			// Removes the original selected triangles and reinserts the remeshed submesh, reusing the exact
			// base-mesh vertex IDs at every boundary vertex it recognizes (the ID-based correspondence recorded
			// above) - this is the actual topology weld: boundary vertices become the same mesh vertex rather
			// than merely coincident duplicates, with no separate spatial-weld pass over the whole mesh.
			bReinsertSucceeded = RegionOp.BackPropropagate();

			if (Settings->RemeshOptions.bAutoCompact)
			{
				EditMesh.CompactInPlace();
			}
		});

		if (!bReinsertSucceeded)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("ReinsertFailed",
				"Remesh: failed to weld the remeshed selection back into the source mesh; some triangles may be missing from the result."), Context);
		}

		EmitOutput(Context, Input, WorkingMesh, Materials);
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
		"Applies Geometry Script Uniform or Adaptive remeshing to an entire Dynamic Mesh, or to only the region "
		"described by a Mesh Selection - extracting, remeshing with a fixed outer boundary, and welding the "
		"region back into the untouched remainder of the source mesh.");
}

EPCGChangeType UPCGRemeshSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGRemeshSettings, InputMode))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGRemeshSettings::InputPinProperties() const
{
	if (InputMode == EPCGRemeshInputMode::MeshSelection)
	{
		return {FPCGPinProperties(SelectionPin, FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
	}
	return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, true, true)};
}

TArray<FPCGPinProperties> UPCGRemeshSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGRemeshSettings::CreateElement() const
{
	return MakeShared<FPCGRemeshElement>();
}

bool FPCGRemeshElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGRemeshElement::ExecuteInternal);
	check(Context);

	const UPCGRemeshSettings* Settings = Context->GetInputSettings<UPCGRemeshSettings>();
	check(Settings);

	const bool bSelectionMode = Settings->InputMode == EPCGRemeshInputMode::MeshSelection;
	const FName InputPin = bSelectionMode ? SelectionPin : PCGPinConstants::DefaultInputLabel;

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(InputPin))
	{
		if (bSelectionMode)
		{
			RemeshSelection(Context, Settings, Input);
		}
		else
		{
			RemeshFullMesh(Context, Settings, Input);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
