#include "Elements/PCGSplineToDynamicMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGSplineData.h"
#include "Engine/StaticMesh.h"
#include "FunctionLibraries/PCGUtilsDynMeshHelpers.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSplineToDynamicMeshElement"

#if WITH_EDITOR
FText UPCGSplineToDynamicMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Spline To Dynamic Mesh");
}

FText UPCGSplineToDynamicMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Bakes the configured static mesh through every segment of each input PCG spline. "
		"Outputs one Dynamic Mesh per segment, or one boolean-unioned Dynamic Mesh per input spline.");
}
#endif

TArray<FPCGPinProperties> UPCGSplineToDynamicMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spline, true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSplineToDynamicMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true);
	return Pins;
}

FPCGElementPtr UPCGSplineToDynamicMeshSettings::CreateElement() const
{
	return MakeShared<FPCGSplineToDynamicMeshElement>();
}

bool FPCGSplineToDynamicMeshElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSplineToDynamicMeshElement::ExecuteInternal);
	check(Context);

	const UPCGSplineToDynamicMeshSettings* Settings = Context->GetInputSettings<UPCGSplineToDynamicMeshSettings>();
	check(Settings);
	if (!IsValid(Settings->StaticMesh))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingStaticMesh", "Spline To Dynamic Mesh requires a Static Mesh."), Context);
		return true;
	}

	TArray<UMaterialInterface*> Materials;
	Materials.Reserve(Settings->StaticMesh->GetStaticMaterials().Num());
	for (const FStaticMaterial& StaticMaterial : Settings->StaticMesh->GetStaticMaterials())
	{
		Materials.Add(StaticMaterial.MaterialInterface);
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSplineData* SplineData = Cast<const UPCGSplineData>(Input.Data);
		if (!SplineData)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidInput", "Spline To Dynamic Mesh skipped a non-spline input."), Context);
			continue;
		}

		const int32 NumSegments = SplineData->GetNumSegments();
		UDynamicMesh* CombinedMesh = nullptr;
		for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
		{
			const float StartKey = static_cast<float>(SegmentIndex);
			const float EndKey = static_cast<float>(SegmentIndex + 1);
			const FTransform StartTransform = SplineData->SplineStruct.GetTransformAtSplineInputKey(
				StartKey, ESplineCoordinateSpace::World, true);
			const FTransform EndTransform = SplineData->SplineStruct.GetTransformAtSplineInputKey(
				EndKey, ESplineCoordinateSpace::World, true);
			const FVector StartTangent = SplineData->SplineStruct.GetTangentAtSplineInputKey(
				StartKey, ESplineCoordinateSpace::World) * Settings->TangentScale;
			const FVector EndTangent = SplineData->SplineStruct.GetTangentAtSplineInputKey(
				EndKey, ESplineCoordinateSpace::World) * Settings->TangentScale;

			UDynamicMesh* SegmentMesh = UPCGUtilsDynMeshHelpers::CreateDynamicMeshFromSplineMesh(
				Settings->StaticMesh, StartTransform, StartTangent, EndTransform, EndTangent,
				Settings->ForwardAxis, Settings->SplineUpDirection, Settings->bSmoothInterpRollScale,
				Settings->RenderLODIndex);
			if (!SegmentMesh)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("SegmentFailed", "Spline To Dynamic Mesh failed to build segment {0}."),
					FText::AsNumber(SegmentIndex)), Context);
				continue;
			}

			if (Settings->bUnionSegments)
			{
				if (!CombinedMesh)
				{
					CombinedMesh = SegmentMesh;
				}
				else
				{
					UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
						CombinedMesh, FTransform::Identity, SegmentMesh, FTransform::Identity,
						EGeometryScriptBooleanOperation::Union, Settings->UnionOptions);
				}
			}
			else
			{
				UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
				OutputData->Initialize(SegmentMesh, true, Materials);
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
				Output.Data = OutputData;
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
			}
		}

		if (Settings->bUnionSegments && CombinedMesh)
		{
			UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
			OutputData->Initialize(CombinedMesh, true, Materials);
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Output.Data = OutputData;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
