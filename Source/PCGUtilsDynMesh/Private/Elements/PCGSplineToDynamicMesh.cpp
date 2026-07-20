#include "Elements/PCGSplineToDynamicMesh.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGSplineData.h"
#include "Engine/StaticMesh.h"
#include "FunctionLibraries/PCGUtilsDynMeshHelpers.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Metadata/PCGMetadata.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSplineToDynamicMeshElement"

namespace
{
	FName GetDataAttributeName(FName ConfiguredName)
	{
		FString Name = ConfiguredName.ToString();
		Name.RemoveFromStart(TEXT("@Data."), ESearchCase::IgnoreCase);
		return FName(Name);
	}

	UStaticMesh* ResolveInputStaticMesh(const UPCGSplineData* SplineData,
		const UPCGSplineToDynamicMeshSettings* Settings, bool& bOutAttributeResolved)
	{
		bOutAttributeResolved = false;
		if (!Settings->bUseDataAttributeStaticMesh || !SplineData || Settings->StaticMeshAttributeName.IsNone())
		{
			return Settings->StaticMesh;
		}

		const UPCGMetadata* Metadata = SplineData->ConstMetadata();
		const FName AttributeName = GetDataAttributeName(Settings->StaticMeshAttributeName);
		const FPCGMetadataAttribute<FSoftObjectPath>* Attribute = Metadata
			? Metadata->GetConstTypedAttribute<FSoftObjectPath>(
				FPCGAttributeIdentifier(AttributeName, PCGMetadataDomainID::Data))
			: nullptr;
		if (!Attribute)
		{
			return Settings->StaticMesh;
		}

		const FSoftObjectPath MeshPath = Attribute->GetValueFromItemKey(PCGInvalidEntryKey);
		if (MeshPath.IsNull())
		{
			return Settings->StaticMesh;
		}

		UObject* MeshObject = MeshPath.ResolveObject();
		if (!MeshObject)
		{
			MeshObject = MeshPath.TryLoad();
		}
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(MeshObject))
		{
			bOutAttributeResolved = true;
			return Mesh;
		}

		return Settings->StaticMesh;
	}

	TArray<UMaterialInterface*> GetStaticMeshMaterials(const UStaticMesh* StaticMesh)
	{
		TArray<UMaterialInterface*> Materials;
		if (!StaticMesh) return Materials;
		Materials.Reserve(StaticMesh->GetStaticMaterials().Num());
		for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
		{
			Materials.Add(StaticMaterial.MaterialInterface);
		}
		return Materials;
	}
}

#if WITH_EDITOR
FText UPCGSplineToDynamicMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "SplineMesh To DynMesh");
}

FText UPCGSplineToDynamicMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Bakes the configured static mesh through every segment of each input PCG spline. "
		"Outputs one Dynamic Mesh per segment, or one boolean-unioned Dynamic Mesh per input spline. "
		"World-space spline data can be converted to the PCG target actor's local space for spawning.");
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
	if (!Settings->bUseDataAttributeStaticMesh && !IsValid(Settings->StaticMesh))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingStaticMesh", "Spline To Dynamic Mesh requires a Static Mesh."), Context);
		return true;
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

		bool bAttributeResolved = false;
		UStaticMesh* InputStaticMesh = ResolveInputStaticMesh(SplineData, Settings, bAttributeResolved);
		if (Settings->bUseDataAttributeStaticMesh && !bAttributeResolved && Settings->bWarnIfAttributeMissing)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("StaticMeshAttributeMissing",
					"Spline To Dynamic Mesh input did not provide a valid Static Mesh in Data attribute '{0}'; using the default Static Mesh."),
				FText::FromName(Settings->StaticMeshAttributeName)), Context);
		}
		if (!IsValid(InputStaticMesh))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("InputMissingStaticMesh", "Spline To Dynamic Mesh input has neither a valid attribute Static Mesh nor a default Static Mesh."),
				Context);
			continue;
		}
		const TArray<UMaterialInterface*> Materials = GetStaticMeshMaterials(InputStaticMesh);

		const int32 NumSegments = SplineData->GetNumSegments();
		FTransform OutputActorTransform = FTransform::Identity;
		FTransform WorldToOutput = FTransform::Identity;
		if (Settings->bConvertWorldToActorLocal)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(SplineData))
			{
				OutputActorTransform = TargetActor->GetActorTransform();
				WorldToOutput = OutputActorTransform.Inverse();
			}
			else
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("MissingTargetActor", "Spline To Dynamic Mesh could not resolve a target actor; output remains in world space."),
					Context);
			}
		}

		const FVector OutputSplineUpDirection = Settings->bConvertWorldToActorLocal
			? WorldToOutput.TransformVectorNoScale(Settings->SplineUpDirection)
			: Settings->SplineUpDirection;
		UDynamicMesh* CombinedMesh = nullptr;
		for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
		{
			const float StartKey = static_cast<float>(SegmentIndex);
			const float EndKey = static_cast<float>(SegmentIndex + 1);
			FTransform StartTransform = SplineData->SplineStruct.GetTransformAtSplineInputKey(
				StartKey, ESplineCoordinateSpace::World, true);
			FTransform EndTransform = SplineData->SplineStruct.GetTransformAtSplineInputKey(
				EndKey, ESplineCoordinateSpace::World, true);
			FVector StartTangent = SplineData->SplineStruct.GetTangentAtSplineInputKey(
				StartKey, ESplineCoordinateSpace::World) * Settings->TangentScale;
			FVector EndTangent = SplineData->SplineStruct.GetTangentAtSplineInputKey(
				EndKey, ESplineCoordinateSpace::World) * Settings->TangentScale;

			if (Settings->bConvertWorldToActorLocal)
			{
				StartTransform = StartTransform.GetRelativeTransform(OutputActorTransform);
				EndTransform = EndTransform.GetRelativeTransform(OutputActorTransform);
				StartTangent = WorldToOutput.TransformVector(StartTangent);
				EndTangent = WorldToOutput.TransformVector(EndTangent);
			}

			UDynamicMesh* SegmentMesh = UPCGUtilsDynMeshHelpers::CreateDynamicMeshFromSplineMesh(
				InputStaticMesh, StartTransform, StartTangent, EndTransform, EndTangent,
				Settings->ForwardAxis, OutputSplineUpDirection, Settings->bSmoothInterpRollScale,
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
