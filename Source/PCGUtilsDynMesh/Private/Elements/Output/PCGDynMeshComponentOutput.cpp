// Copyright Max Harris
#include "Elements/Output/PCGDynMeshComponentOutput.h"

#include "Components/DynamicMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshComponentOutput"

void UPCGDynMeshOutputManagedComponent::MarkAsReused()
{
	// The engine DynMesh resource resets to translation only. Keep our complete coordinate frame instead.
	UPCGManagedResource::MarkAsReused();
	if (UDynamicMeshComponent* Component = GetComponent())
	{
		Component->UnregisterComponent();
		Component->SetWorldTransform(FrameActor.IsValid() ? FrameActor->GetActorTransform() : MeshToWorld);
		Component->RegisterComponent();
	}
}

TArray<FPCGPinProperties> UPCGDynMeshComponentOutputSettings::InputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, true, true)};
}

TArray<FPCGPinProperties> UPCGDynMeshComponentOutputSettings::OutputPinProperties() const
{
	return IsDebugOutput() ? TArray<FPCGPinProperties>()
		: TArray<FPCGPinProperties>{FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGDynMeshComponentOutputSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshComponentOutputElement>();
}

bool FPCGDynMeshComponentOutputElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const auto* Settings = Context->GetInputSettings<UPCGDynMeshComponentOutputSettings>();
	check(Settings);
#if !WITH_EDITOR
	if (Settings->IsDebugOutput())
	{
		return true;
	}
#endif
	UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context->ExecutionSource.Get());
	if (!SourceComponent)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoComponent", "DynMesh component output needs a PCG component execution source; no components were created."), Context);
		return true;
	}
	AActor* Owner = Settings->TargetActor.Get();
	if (!Owner)
	{
		Owner = Context->GetTypedExecutionTarget<AActor>();
	}
	if (!Owner || !Owner->GetRootComponent())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoOwner", "DynMesh component output could not resolve an owning actor with a root component; no components were created."), Context);
		return true;
	}
	const auto Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const auto* MeshData = Cast<UPCGDynamicMeshData>(Inputs[Index].Data);
		if (!MeshData || !MeshData->GetDynamicMesh())
		{
			PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("InvalidMesh", "DynMesh component output: input {0} has no DynMesh; skipped."), FText::AsNumber(Index)), Context);
			continue;
		}
		UPCGDynMeshOutputManagedComponent* Resource = nullptr;
		SourceComponent->ForEachManagedResource([&](UPCGManagedResource* Candidate)
		{
			auto* Output = Cast<UPCGDynMeshOutputManagedComponent>(Candidate);
			if (Output && Output->CanBeUsed() && Output->OutputSettings == Settings && Output->InputIndex == Index
				&& Output->GetComponent() && Output->GetComponent()->GetOwner() == Owner)
			{
				Resource = Output;
			}
		});
		if (!Resource)
		{
			Resource = PCGUtilsAsyncObjects::NewObject<UPCGDynMeshOutputManagedComponent>(Context, SourceComponent);
			Resource->OutputSettings = Settings;
			Resource->InputIndex = Index;
			SourceComponent->AddToManagedResources(Resource);
			auto* Component = PCGUtilsAsyncObjects::NewObject<UDynamicMeshComponent>(Context, Owner);
			Component->ComponentTags.AddUnique(SourceComponent->GetFName());
			Component->ComponentTags.AddUnique(PCGHelpers::DefaultPCGTag);
			if (Settings->IsDebugOutput())
			{
				Component->ComponentTags.AddUnique(PCGHelpers::DefaultPCGDebugTag);
			}
			Owner->AddInstanceComponent(Component);
			Component->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			Resource->SetComponent(Component);
		}
		Resource->MarkAsUsed();
#if WITH_EDITOR
		Resource->ChangeTransientState(Settings->IsDebugOutput() ? EPCGEditorDirtyMode::Preview : SourceComponent->GetEditingMode());
#endif
		Resource->MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, MeshData, Settings->bInputIsActorLocal);
		Resource->FrameActor = Settings->bInputIsActorLocal ? Context->GetTargetActor(MeshData) : nullptr;
		UDynamicMeshComponent* Component = Resource->GetComponent();
		SourceComponent->IgnoreChangeOriginDuringGenerationWithScope(Component, [&]()
		{
			Component->UnregisterComponent();
			// Different ownership must not compose this frame through an unrelated scaled parent.
			const bool bIndependentFrame = Resource->FrameActor.Get() != Owner;
			Component->SetAbsolute(bIndependentFrame, bIndependentFrame, bIndependentFrame);
			Component->SetWorldTransform(Resource->MeshToWorld);
			MeshData->InitializeDynamicMeshComponentFromData(Component);
			Component->ComponentTags.SetNum(0);
			Component->ComponentTags.AddUnique(SourceComponent->GetFName());
			Component->ComponentTags.AddUnique(PCGHelpers::DefaultPCGTag);
			if (Settings->IsDebugOutput()) Component->ComponentTags.AddUnique(PCGHelpers::DefaultPCGDebugTag);
			for (const FString& Tag : Inputs[Index].Tags) Component->ComponentTags.AddUnique(FName(*Tag));
			Component->RegisterComponent();
		});
		if (!Settings->IsDebugOutput())
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
