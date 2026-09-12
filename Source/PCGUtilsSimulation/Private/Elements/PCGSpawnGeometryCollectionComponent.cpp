// Copyright Max Harris

#include "Elements/PCGSpawnGeometryCollectionComponent.h"

#include "Chaos/ChaosSolverActor.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Elements/Conversion/PCGSaveGeometryCollectionToAsset.h"
#include "Engine/Engine.h"
#include "Field/FieldSystemActor.h"
#include "Field/FieldSystemComponent.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/PCGMetadata.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGManagedResource.h"
#include "PCGManagedResourceContainer.h"
#include "PCGPin.h"
#include "PCGUtilsSimulationModule.h"
#include "Utils/PCGLogErrors.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSpawnGeometryCollectionComponent)

#define LOCTEXT_NAMESPACE "PCGSpawnGeometryCollectionComponent"

namespace PCGSpawnGeometryCollectionComponent
{
	void ConfigureComponent(
		UGeometryCollectionComponent& Component,
		const UGeometryCollection* Asset,
		bool bApplyAssetDefaults,
		const TArray<TObjectPtr<const AFieldSystemActor>>* Fields,
		bool bSetSolver,
		AChaosSolverActor* Solver)
	{
		check(!Component.IsRegistered());

		if (Asset)
		{
			// SetRestCollection also resets the dynamic collection and embedded geometry. On an unregistered
			// component its RecreatePhysicsState is a no-op, and OnRegister resets the dynamic collection again.
			Component.SetRestCollection(Asset, /*bApplyAssetDefaults=*/false);
			if (bApplyAssetDefaults)
			{
				Component.ApplyAssetDefaults();
			}
		}

		if (Fields)
		{
			Component.InitializationFields = *Fields;
		}

		if (bSetSolver)
		{
			// Direct assignment: SetSolverActor calls ResetState, which is meant for a live component.
			Component.ChaosSolverActor = Solver;
		}
	}

	bool HasConstructionFields(const AFieldSystemActor& FieldActor)
	{
		// Mirrors UGeometryCollectionComponent::GetInitializationCommands: construction commands first, the legacy
		// UFieldSystem asset as the fallback.
		const UFieldSystemComponent* FieldComponent = FieldActor.GetFieldSystemComponent();
		return FieldComponent &&
			(FieldComponent->ConstructionCommands.GetNumCommands() > 0 || FieldComponent->GetFieldSystem() != nullptr);
	}
}

namespace PCGSpawnGeometryCollectionComponentPrivate
{
	/** Reads a soft object path per entry. Returns false when the data has no readable attribute by that name. */
	bool ReadSpawnGCSoftPaths(
		const UPCGData* Data, const FPCGAttributePropertyInputSelector& InSelector, TArray<FSoftObjectPath>& OutPaths)
	{
		OutPaths.Reset();

		const FPCGAttributePropertyInputSelector Selector = InSelector.CopyAndFixLast(Data);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor =
			PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
		if (!Accessor.IsValid() || !Keys.IsValid())
		{
			return false;
		}

		OutPaths.SetNum(Keys->GetNum());
		return OutPaths.IsEmpty() || Accessor->GetRange<FSoftObjectPath>(
			MakeArrayView(OutPaths), 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
	}

	/** Resolves every actor referenced on a pin, deduplicated, warning about what could not be resolved. */
	void ResolveSpawnGCActors(
		FPCGContext* Context,
		const TArray<FPCGTaggedData>& Inputs,
		const FPCGAttributePropertyInputSelector& Selector,
		const FName PinLabel,
		TArray<AActor*>& OutActors)
	{
		int32 NumUnresolved = 0;
		for (const FPCGTaggedData& Input : Inputs)
		{
			if (!Input.Data)
			{
				continue;
			}

			TArray<FSoftObjectPath> Paths;
			if (!ReadSpawnGCSoftPaths(Input.Data, Selector, Paths))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("MissingActorAttribute", "{0}: input data has no actor reference attribute '{1}'."),
					FText::FromName(PinLabel), Selector.GetDisplayText()), Context);
				continue;
			}

			for (const FSoftObjectPath& Path : Paths)
			{
				if (Path.IsNull())
				{
					continue;
				}

				if (AActor* Actor = Cast<AActor>(Path.ResolveObject()))
				{
					OutActors.AddUnique(Actor);
				}
				else
				{
					++NumUnresolved;
				}
			}
		}

		if (NumUnresolved > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("UnresolvedActors", "{0}: {1} actor reference(s) do not resolve to a loaded actor and were ignored."),
				FText::FromName(PinLabel), FText::AsNumber(NumUnresolved)), Context);
		}
	}

	void PassThroughSpawnGCInput(FPCGContext* Context, const FPCGTaggedData& Input)
	{
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}
}

UPCGSpawnGeometryCollectionComponentSettings::UPCGSpawnGeometryCollectionComponentSettings(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The contract with GC | Save Asset, which writes exactly this attribute.
	AssetAttribute.SetAttributeName(PCGSaveGeometryCollectionToAssetConstants::AssetPathAttribute);
	TransformAttribute.Update(FString(TEXT("$Transform")));

	// The contract with Spawn Actor and Get Actor Data, which write actor references under this name.
	FieldActorAttribute.SetAttributeName(PCGPointDataConstants::ActorReferenceAttribute);
	SolverActorAttribute.SetAttributeName(PCGPointDataConstants::ActorReferenceAttribute);

	ComponentReferenceAttribute.SetAttributeName(PCGSpawnGeometryCollectionComponentConstants::ComponentReferenceAttribute);

	TemplateComponent = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("TemplateComponent"));
}

#if WITH_EDITOR
FText UPCGSpawnGeometryCollectionComponentSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Spawn Component");
}

FText UPCGSpawnGeometryCollectionComponentSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Spawns a PCG-managed Geometry Collection Component for each entry of Asset - for example the AssetPath "
		"output of GC | Save Asset. The components are regenerated with the graph and removed on cleanup.\n\n"
		"Enable Init Fields to wire Field System Actors (an anchor field from Spawn Actor, say) into every "
		"component's Initialization Fields, and Solver to override its Chaos solver. Both are applied before the "
		"component registers, which is when Chaos reads them.");
}

EPCGChangeType UPCGSpawnGeometryCollectionComponentSettings::GetChangeTypeForProperty(const FName& InPropertyName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(InPropertyName);
	if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSpawnGeometryCollectionComponentSettings, bUseInitializationFields) ||
		InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSpawnGeometryCollectionComponentSettings, bUseSolver))
	{
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

TArray<FPCGPinProperties> UPCGSpawnGeometryCollectionComponentSettings::InputPinProperties() const
{
	using namespace PCGSpawnGeometryCollectionComponentConstants;

	TArray<FPCGPinProperties> Pins;

	FPCGPinProperties& AssetPin = Pins.Emplace_GetRef(AssetInputPin, EPCGDataType::PointOrParam);
	AssetPin.SetRequiredPin();
#if WITH_EDITOR
	AssetPin.Tooltip = LOCTEXT("AssetPinTooltip",
		"One component per entry. Points place each component at the point; an attribute set places it at the "
		"target actor.");
#endif

	if (bUseInitializationFields)
	{
		FPCGPinProperties& FieldsPin = Pins.Emplace_GetRef(FieldsInputPin, EPCGDataType::PointOrParam);
#if WITH_EDITOR
		FieldsPin.Tooltip = LOCTEXT("FieldsPinTooltip",
			"Field System Actors applied to every spawned component as Initialization Fields. Only construction "
			"fields count - the ones a field Blueprint adds with Add Construction Field.");
#endif
	}

	if (bUseSolver)
	{
		FPCGPinProperties& SolverPin = Pins.Emplace_GetRef(
			SolverInputPin, EPCGDataType::PointOrParam, /*bInAllowMultipleConnections=*/false);
#if WITH_EDITOR
		SolverPin.Tooltip = LOCTEXT("SolverPinTooltip",
			"One Chaos Solver Actor used instead of the world solver. Runtime fields reach it only if the field "
			"lists it in Supported Solvers.");
#endif
	}

	return Pins;
}

TArray<FPCGPinProperties> UPCGSpawnGeometryCollectionComponentSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::PointOrParam);
	return Pins;
}

FPCGElementPtr UPCGSpawnGeometryCollectionComponentSettings::CreateElement() const
{
	return MakeShared<FPCGSpawnGeometryCollectionComponentElement>();
}

bool FPCGSpawnGeometryCollectionComponentElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSpawnGeometryCollectionComponentElement::Execute);

	using namespace PCGSpawnGeometryCollectionComponentConstants;
	using namespace PCGSpawnGeometryCollectionComponentPrivate;

	check(InContext);
	FPCGSpawnGeometryCollectionComponentContext* Context =
		static_cast<FPCGSpawnGeometryCollectionComponentContext*>(InContext);

	const UPCGSpawnGeometryCollectionComponentSettings* Settings =
		Context->GetInputSettings<UPCGSpawnGeometryCollectionComponentSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(AssetInputPin);
	if (Inputs.IsEmpty())
	{
		return true;
	}

	// Load every referenced asset before touching the world. Asynchronous by default; when it pauses, the element
	// is re-entered with WasLoadRequested() true and continues below.
	if (!Context->WasLoadRequested())
	{
		TArray<FSoftObjectPath> ObjectsToLoad;
		for (const FPCGTaggedData& Input : Inputs)
		{
			TArray<FSoftObjectPath> Paths;
			if (Input.Data && ReadSpawnGCSoftPaths(Input.Data, Settings->AssetAttribute, Paths))
			{
				for (const FSoftObjectPath& Path : Paths)
				{
					if (!Path.IsNull())
					{
						ObjectsToLoad.AddUnique(Path);
					}
				}
			}
		}

		if (!Context->RequestResourceLoad(Context, MoveTemp(ObjectsToLoad), !Settings->bSynchronousLoad))
		{
			return false;
		}
	}

	IPCGGraphExecutionSource* ExecutionSource = Context->ExecutionSource.Get();
	UObject* ExecutionSourceObject = Cast<UObject>(ExecutionSource);
	AActor* TargetActor = ExecutionSource ? Cast<AActor>(ExecutionSource->GetExecutionState().GetTarget()) : nullptr;
	if (!TargetActor || !ExecutionSourceObject)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoTargetActor", "GC | Spawn Component needs an execution source with a target actor to own the components."),
			Context);
		return true;
	}

	FPCGManagedResourceContainerHelper ResourceHelper(ExecutionSource);
	if (!ResourceHelper.IsValid())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoResourceContainer", "GC | Spawn Component needs an execution source that tracks generated resources, or its components could never be cleaned up."),
			Context);
		return true;
	}

	// Fields and solver are resolved once and applied to every component.
	TArray<TObjectPtr<const AFieldSystemActor>> Fields;
	if (Settings->bUseInitializationFields)
	{
		const TArray<FPCGTaggedData> FieldInputs = Context->InputData.GetInputsByPin(FieldsInputPin);
		TArray<AActor*> Actors;
		ResolveSpawnGCActors(Context, FieldInputs, Settings->FieldActorAttribute, FieldsInputPin, Actors);

		for (AActor* Actor : Actors)
		{
			const AFieldSystemActor* FieldActor = Cast<AFieldSystemActor>(Actor);
			if (!FieldActor)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NotAFieldActor", "Init Fields: '{0}' is not a Field System Actor and was ignored."),
					FText::FromString(Actor->GetActorNameOrLabel())), Context);
				continue;
			}

			if (!PCGSpawnGeometryCollectionComponent::HasConstructionFields(*FieldActor))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NoConstructionFields", "Init Fields: '{0}' has no construction fields, so it will not affect the simulation's initial state. Initialization Fields only use fields added with Add Construction Field."),
					FText::FromString(FieldActor->GetActorNameOrLabel())), Context);
			}

			Fields.Add(FieldActor);
		}

		if (!FieldInputs.IsEmpty() && Fields.IsEmpty())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("NoFieldsResolved", "Init Fields received data but no Field System Actor. If it comes from Spawn Actor, make sure Spawn Actor is not set to Collapse Actors, which writes no actor references."),
				Context);
		}
	}

	AChaosSolverActor* Solver = nullptr;
	if (Settings->bUseSolver)
	{
		const TArray<FPCGTaggedData> SolverInputs = Context->InputData.GetInputsByPin(SolverInputPin);
		TArray<AActor*> Actors;
		ResolveSpawnGCActors(Context, SolverInputs, Settings->SolverActorAttribute, SolverInputPin, Actors);

		if (!SolverInputs.IsEmpty() && Actors.Num() != 1)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("SolverCount", "Solver expects exactly one actor but resolved {0}.{1}"),
				FText::AsNumber(Actors.Num()),
				Actors.Num() > 1 ? LOCTEXT("SolverUsingFirst", " Using the first.") : FText::GetEmpty()), Context);
		}

		if (!Actors.IsEmpty())
		{
			Solver = Cast<AChaosSolverActor>(Actors[0]);
			if (!Solver)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NotASolver", "Solver: '{0}' is not a Chaos Solver Actor; the world solver is used instead."),
					FText::FromString(Actors[0]->GetActorNameOrLabel())), Context);
			}
		}
	}

	UGeometryCollectionComponent* Template = Settings->TemplateComponent;
	const UGeometryCollection* TemplateAsset = Template ? Template->GetRestCollection() : nullptr;
	USceneComponent* Root = TargetActor->GetRootComponent();
	const FName SourceTag = ExecutionSourceObject->GetFName();
	const EObjectFlags ObjectFlags =
		ExecutionSource->GetExecutionState().IsInPreviewMode() ? RF_Transient : RF_NoFlags;

	// One resource per execution: every component from this node is released together.
	UPCGManagedComponentList* Resource = NewObject<UPCGManagedComponentList>(ExecutionSourceObject);

#if WITH_EDITOR
	FScopedTransaction Transaction(
		LOCTEXT("SpawnComponents", "Spawning Geometry Collection Components from PCG"),
		ExecutionSource->GetExecutionState().UseTransactions());
#endif

	int32 NumSpawned = 0;
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGData* Data = Input.Data;
		if (!Data)
		{
			continue;
		}

		TArray<FSoftObjectPath> AssetPaths;
		const bool bHasAssetAttribute = ReadSpawnGCSoftPaths(Data, Settings->AssetAttribute, AssetPaths);
		if (!bHasAssetAttribute && !TemplateAsset)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingAssetAttribute", "Input has no asset attribute '{0}' and the Template Component has no Rest Collection. Nothing spawned."),
				Settings->AssetAttribute.GetDisplayText()), Context);
			PassThroughSpawnGCInput(Context, Input);
			continue;
		}

		// Points carry their own transform. An attribute set without one spawns at the target actor.
		TArray<FTransform> Transforms;
		bool bAtTarget = false;
		{
			const FPCGAttributePropertyInputSelector Selector = Settings->TransformAttribute.CopyAndFixLast(Data);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor =
				PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys =
				PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);

			if (Accessor.IsValid() && Keys.IsValid())
			{
				Transforms.SetNum(Keys->GetNum());
				if (!Transforms.IsEmpty() && !Accessor->GetRange<FTransform>(
					MakeArrayView(Transforms), 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("InvalidTransformAttribute", "Transform attribute '{0}' is not compatible with a transform. Nothing spawned."),
						Selector.GetDisplayText()), Context);
					PassThroughSpawnGCInput(Context, Input);
					continue;
				}
			}
			else if (!Data->IsA<UPCGSpatialData>())
			{
				bAtTarget = true;
			}
			else
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("MissingTransformAttribute", "Input has no transform attribute '{0}'. Nothing spawned."),
					Selector.GetDisplayText()), Context);
				PassThroughSpawnGCInput(Context, Input);
				continue;
			}
		}

		const int32 NumEntries = bHasAssetAttribute ? AssetPaths.Num() : Transforms.Num();
		if (!bAtTarget && Transforms.Num() != NumEntries && Transforms.Num() != 1)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("TransformCountMismatch", "Transform count {0} does not match asset count {1}. Nothing spawned."),
				FText::AsNumber(Transforms.Num()), FText::AsNumber(NumEntries)), Context);
			PassThroughSpawnGCInput(Context, Input);
			continue;
		}

		UPCGData* OutputData = Data->DuplicateData(Context);
		const FPCGAttributePropertyOutputNoSourceSelector& ReferenceSelector = Settings->ComponentReferenceAttribute;
		if (ReferenceSelector.IsBasicAttribute() && ReferenceSelector.GetName() != NAME_None)
		{
			OutputData->MutableMetadata()->FindOrCreateAttribute(ReferenceSelector.GetName(), FSoftObjectPath(),
				/*bAllowsInterpolation=*/false, /*bOverrideParent=*/false, /*bOverwriteIfTypeMismatch=*/false);
		}

		const TUniquePtr<IPCGAttributeAccessor> ReferenceAccessor =
			PCGAttributeAccessorHelpers::CreateAccessor(OutputData, ReferenceSelector);
		const TUniquePtr<IPCGAttributeAccessorKeys> ReferenceKeys =
			PCGAttributeAccessorHelpers::CreateKeys(OutputData, ReferenceSelector);
		if (!ReferenceAccessor.IsValid() || !ReferenceKeys.IsValid())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("CannotWriteReferences", "Unable to write component references to '{0}'."),
				ReferenceSelector.GetDisplayText()), Context);
		}

		TArray<FSoftObjectPath> References;
		References.SetNum(NumEntries);

		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			const UGeometryCollection* Asset = TemplateAsset;
			if (bHasAssetAttribute && !AssetPaths[Index].IsNull())
			{
				Asset = Cast<UGeometryCollection>(AssetPaths[Index].ResolveObject());
				if (!Asset)
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("NotAGeometryCollection", "Entry {0}: '{1}' is not a Geometry Collection asset. Skipped."),
						FText::AsNumber(Index), FText::FromString(AssetPaths[Index].ToString())), Context);
					continue;
				}
			}

			if (!Asset)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NoAsset", "Entry {0} has no asset and the Template Component has no Rest Collection. Skipped."),
					FText::AsNumber(Index)), Context);
				continue;
			}

			// Copy rather than pass the template as an archetype: an archetype living in a graph asset would make
			// every generated level component depend on that package.
			UGeometryCollectionComponent* Component =
				NewObject<UGeometryCollectionComponent>(TargetActor, NAME_None, ObjectFlags);
			if (Template)
			{
				UEngine::CopyPropertiesForUnrelatedObjects(Template, Component);
			}

			PCGSpawnGeometryCollectionComponent::ConfigureComponent(*Component, Asset, Settings->bApplyAssetDefaults,
				Settings->bUseInitializationFields ? &Fields : nullptr, Settings->bUseSolver, Solver);

			if (!Settings->PropertyOverrideDescriptions.IsEmpty())
			{
				FPCGObjectOverrides<UGeometryCollectionComponent> Overrides(Component);
				Overrides.Initialize(Settings->PropertyOverrideDescriptions, Component, Data, Context);
				if (Overrides.IsValid() && !Overrides.Apply(Index))
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("OverrideFailed", "Entry {0}: failed to apply property overrides."),
						FText::AsNumber(Index)), Context);
				}
			}

			// Placement also happens before registration: Add Component moves the component after RegisterComponent,
			// but in a game world that is after the physics proxy has already been created at the old transform.
			if (Root)
			{
				Component->SetupAttachment(Root);
			}

			if (bAtTarget)
			{
				if (Root)
				{
					Component->SetRelativeTransform(FTransform::Identity);
				}
				else
				{
					Component->SetWorldTransform(TargetActor->GetActorTransform());
				}
			}
			else
			{
				Component->SetWorldTransform(Transforms[Transforms.Num() == 1 ? 0 : Index]);
			}

			Component->ComponentTags.AddUnique(SourceTag);
			Component->ComponentTags.AddUnique(PCGHelpers::DefaultPCGTag);

			Component->RegisterComponent();
			TargetActor->AddInstanceComponent(Component);

			References[Index] = FSoftObjectPath(Component);
			Resource->GeneratedComponents.Emplace(Component);
			++NumSpawned;
		}

		if (ReferenceAccessor.IsValid() && ReferenceKeys.IsValid() && NumEntries > 0)
		{
			ReferenceAccessor->SetRange<FSoftObjectPath>(MakeArrayView(References), 0, *ReferenceKeys,
				EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	if (!Resource->GeneratedComponents.IsEmpty())
	{
		ResourceHelper.AddManagedResource(Resource);
	}

	UE_LOG(LogPCGUtilsSimulation, Log,
		TEXT("GC | Spawn Component: spawned %d component(s) on '%s' with %d initialization field(s)%s."),
		NumSpawned, *TargetActor->GetActorNameOrLabel(), Fields.Num(),
		Solver ? *FString::Printf(TEXT(" and solver '%s'"), *Solver->GetActorNameOrLabel()) : TEXT(""));

	return true;
}

#undef LOCTEXT_NAMESPACE
