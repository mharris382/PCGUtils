#include "Elements/PCGDynMeshActorSpaceTransform.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDynMeshActorSpaceTransform)

#define LOCTEXT_NAMESPACE "PCGDynMeshActorSpaceTransform"

namespace
{
	constexpr int32 ToWorldAlias = 0;
	constexpr int32 ToLocalAlias = 1;
}

TArray<FPCGPinProperties> UPCGDynMeshActorSpaceTransformSettings::InputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, true, true)};
}

TArray<FPCGPinProperties> UPCGDynMeshActorSpaceTransformSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

#if WITH_EDITOR
FName UPCGDynMeshActorSpaceTransformSettings::GetDefaultNodeName() const
{
	return Direction == EPCGDynMeshActorSpaceTransformDirection::ToWorld ? TEXT("ToWorld") : TEXT("ToLocal");
}

FText UPCGDynMeshActorSpaceTransformSettings::GetDefaultNodeTitle() const
{
	return Direction == EPCGDynMeshActorSpaceTransformDirection::ToWorld
		? LOCTEXT("ToWorldTitle", "ToWorld")
		: LOCTEXT("ToLocalTitle", "ToLocal");
}

FText UPCGDynMeshActorSpaceTransformSettings::GetNodeTooltipText() const
{
	return Direction == EPCGDynMeshActorSpaceTransformDirection::ToWorld
		? LOCTEXT("ToWorldTooltip", "Transforms Dynamic Mesh data from actor-local space to world space using the PCG target actor transform.")
		: LOCTEXT("ToLocalTooltip", "Transforms Dynamic Mesh data from world space to actor-local space using the inverse PCG target actor transform.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGDynMeshActorSpaceTransformSettings::GetPreconfiguredInfo() const
{
	return {
		{ToWorldAlias, LOCTEXT("ToWorldAlias", "ToWorld"), LOCTEXT("ToWorldAliasTooltip", "Transforms a Dynamic Mesh from actor-local space to world space.")},
		{ToLocalAlias, LOCTEXT("ToLocalAlias", "ToLocal"), LOCTEXT("ToLocalAliasTooltip", "Transforms a Dynamic Mesh from world space to actor-local space.")}
	};
}

void UPCGDynMeshActorSpaceTransformSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Direction = PreconfiguredInfo.PreconfiguredIndex == ToLocalAlias
		? EPCGDynMeshActorSpaceTransformDirection::ToLocal
		: EPCGDynMeshActorSpaceTransformDirection::ToWorld;
}
#endif

FPCGElementPtr UPCGDynMeshActorSpaceTransformSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshActorSpaceTransformElement>();
}

bool FPCGDynMeshActorSpaceTransformElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynMeshActorSpaceTransformSettings* Settings = Context->GetInputSettings<UPCGDynMeshActorSpaceTransformSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		const UPCGDynamicMeshData* InputData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = InputData ? InputData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "ToWorld/ToLocal skipped an invalid Dynamic Mesh input."), Context);
			continue;
		}

		const AActor* TargetActor = Context->GetTargetActor(InputData);
		if (!TargetActor)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingActor", "ToWorld/ToLocal could not resolve the PCG target actor; no output was produced for this input."), Context);
			continue;
		}

		FTransform ActorTransform = TargetActor->GetActorTransform();
		if (Settings->bIgnoreRotation) ActorTransform.SetRotation(FQuat::Identity);
		if (Settings->bIgnoreScale) ActorTransform.SetScale3D(FVector::OneVector);
		if (Settings->GetDirection() == EPCGDynMeshActorSpaceTransformDirection::ToLocal)
		{
			ActorTransform = ActorTransform.Inverse();
		}

		UE::Geometry::FDynamicMesh3 OutputMesh(*SourceMesh);
		MeshTransforms::ApplyTransform(OutputMesh, UE::Geometry::FTransformSRT3d(ActorTransform), true);

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(InputData->GetMaterials().Num());
		for (UMaterialInterface* Material : InputData->GetMaterials()) Materials.Add(Material);

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(OutputMesh), Materials);
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
