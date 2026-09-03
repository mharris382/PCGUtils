// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshSelectionFactoryGroup.h"

#include "Factories/PCGUtilsDynMeshFactories.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSelectionFactoryGroup"

namespace
{
	class FSelectionFactoryGroupOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectionFactoryGroupOperation(const UPCGDynMeshSelectionFactoryGroupData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			if (Factory->ChildFactories.IsEmpty() ||
				(Factory->Mode == EPCGUtilsDynMeshSelectionFactoryGroupMode::Not && Factory->ChildFactories.Num() != 1))
			{
				return false;
			}

			TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> OrderedChildFactories;
			OrderedChildFactories.Reserve(Factory->ChildFactories.Num());
			for (const UPCGUtilsDynMeshSelectionFactoryData* ChildFactory : Factory->ChildFactories)
			{
				if (!ChildFactory)
				{
					return false;
				}
				OrderedChildFactories.Add(ChildFactory);
			}

			// Groups use priority as short-circuit precedence. Keep equal-priority selectors in connection order.
			OrderedChildFactories.StableSort([](const UPCGUtilsDynMeshSelectionFactoryData& A,
				const UPCGUtilsDynMeshSelectionFactoryData& B)
			{
				return A.Priority > B.Priority;
			});

			ChildOperations.Reserve(OrderedChildFactories.Num());
			for (const UPCGUtilsDynMeshSelectionFactoryData* ChildFactory : OrderedChildFactories)
			{
				TSharedPtr<FPCGUtilsDynMeshSelectionOperation> ChildOperation = ChildFactory->CreateOperation(Context);
				if (!ChildOperation || !ChildOperation->Initialize(InSelectionContext))
				{
					return false;
				}
				ChildOperations.Add(MoveTemp(ChildOperation));
			}

			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			switch (Factory->Mode)
			{
			case EPCGUtilsDynMeshSelectionFactoryGroupMode::And:
				for (const TSharedPtr<FPCGUtilsDynMeshSelectionOperation>& Child : ChildOperations)
				{
					if (!Child->TestElement(ElementID))
					{
						return false;
					}
				}
				return true;

			case EPCGUtilsDynMeshSelectionFactoryGroupMode::Or:
				for (const TSharedPtr<FPCGUtilsDynMeshSelectionOperation>& Child : ChildOperations)
				{
					if (Child->TestElement(ElementID))
					{
						return true;
					}
				}
				return false;

			case EPCGUtilsDynMeshSelectionFactoryGroupMode::Not:
				return !ChildOperations[0]->TestElement(ElementID);
			default:
				return false;
			}
		}

	private:
		TObjectPtr<const UPCGDynMeshSelectionFactoryGroupData> Factory;
		TArray<TSharedPtr<FPCGUtilsDynMeshSelectionOperation>> ChildOperations;
	};
}

bool UPCGDynMeshSelectionFactoryGroupData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	if (ChildFactories.IsEmpty() ||
		(Mode == EPCGUtilsDynMeshSelectionFactoryGroupMode::Not && ChildFactories.Num() != 1))
	{
		return false;
	}

	for (const UPCGUtilsDynMeshSelectionFactoryData* Child : ChildFactories)
	{
		if (!Child || !Child->SupportsDomain(Domain))
		{
			return false;
		}
	}
	return true;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshSelectionFactoryGroupData::CreateOperationInternal() const
{
	return MakeShared<FSelectionFactoryGroupOperation>(this);
}

void UPCGDynMeshSelectionFactoryGroupData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 ModeValue = static_cast<uint8>(Mode);
	Ar << ModeValue;
	TArray<uint32> ChildCrcs;
	ChildCrcs.Reserve(ChildFactories.Num());
	for (const UPCGUtilsDynMeshSelectionFactoryData* Child : ChildFactories)
	{
		ChildCrcs.Add(Child ? Child->GetOrComputeCrc(true).GetValue() : 0);
	}
	Ar << ChildCrcs;
}

#if WITH_EDITOR
FText UPCGDynMeshSelectionFactoryGroupProviderSettings::GetDefaultNodeTitle() const
{
	switch (Mode)
	{
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::And:
		return LOCTEXT("AndTitle", "AND");
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::Or:
		return LOCTEXT("OrTitle", "OR");
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::Not:
		return LOCTEXT("NotTitle", "NOT");
	default:
		return LOCTEXT("Title", "Selection Logic");
	}
}

FText UPCGDynMeshSelectionFactoryGroupProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Combines child DynMesh selectors into a nested AND, OR, or NOT predicate. "
		"Higher-priority selectors evaluate first; AND and OR stop evaluating an element as soon as its result is known.");
}

TArray<FText> UPCGDynMeshSelectionFactoryGroupProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("AndAlias", "Intersect Selectors"),
		LOCTEXT("IntersectAlias", "Intersect Selectors"),
		LOCTEXT("OrAlias", "Add Selectors"),
		LOCTEXT("UnionAlias", "Union Selectors"),
		LOCTEXT("NotAlias", "Invert Selector")
	};
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGDynMeshSelectionFactoryGroupProviderSettings::GetPreconfiguredInfo() const
{
	return {
		{static_cast<int32>(EPCGUtilsDynMeshSelectionFactoryGroupMode::And),
			LOCTEXT("AndPreconfiguredTitle", "Selectors AND"),
			LOCTEXT("AndPreconfiguredTooltip", "Keeps an element only when every child selector passes.")},
		{static_cast<int32>(EPCGUtilsDynMeshSelectionFactoryGroupMode::Or),
			LOCTEXT("OrPreconfiguredTitle", "Selectors OR"),
			LOCTEXT("OrPreconfiguredTooltip", "Keeps an element when any child selector passes.")},
		{static_cast<int32>(EPCGUtilsDynMeshSelectionFactoryGroupMode::Not),
			LOCTEXT("NotPreconfiguredTitle", "Selector NOT"),
			LOCTEXT("NotPreconfiguredTooltip", "Inverts the result of one child selector.")}
	};
}

void UPCGDynMeshSelectionFactoryGroupProviderSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	switch (static_cast<EPCGUtilsDynMeshSelectionFactoryGroupMode>(PreconfiguredInfo.PreconfiguredIndex))
	{
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::And:
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::Or:
	case EPCGUtilsDynMeshSelectionFactoryGroupMode::Not:
		Mode = static_cast<EPCGUtilsDynMeshSelectionFactoryGroupMode>(PreconfiguredInfo.PreconfiguredIndex);
		break;
	default:
		ensureMsgf(false, TEXT("Unknown DynMesh Selection Logic preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

FName UPCGDynMeshSelectionFactoryGroupProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshSelectionFactoryGroupProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshSelectionFactoryGroupProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshSelectionFactoryGroupProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> Children;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin, Children,
		PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return nullptr;
	}

	if (Mode == EPCGUtilsDynMeshSelectionFactoryGroupMode::Not && Children.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NotRequiresOneChild", "A NOT selector group requires exactly one child selector."),
			InContext);
		return nullptr;
	}

	UPCGDynMeshSelectionFactoryGroupData* Factory = InFactory
		? Cast<UPCGDynMeshSelectionFactoryGroupData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshSelectionFactoryGroupData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Mode = Mode;
	Factory->ChildFactories = MoveTemp(Children);
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE
