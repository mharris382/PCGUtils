// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshSelectionPainterSwitch.h"

#include "Elements/Painters/PCGUtilsPainterConstantOperation.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSelectionPainterSwitch"

namespace
{
	const FName SelectorPin = TEXT("Selector");
	const FName SelectedPainterPin = TEXT("Selected Painter");
	const FName UnselectedPainterPin = TEXT("Unselected Painter");

	class FSelectionPainterSwitchOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FSelectionPainterSwitchOperation(const UPCGDynMeshSelectionPainterSwitchFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory)
			{
				return false;
			}
			if (!Factory->Selector)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoSelector", "Selection Painter Switch requires a Selector on its Selector pin."), Context);
				return false;
			}

			SelectedBranch = MakeBranchOperation(
				Factory->SelectedSource, Factory->SelectedConstant, Factory->SelectedPainter,
				LOCTEXT("SelectedBranchName", "Selected"));
			UnselectedBranch = MakeBranchOperation(
				Factory->UnselectedSource, Factory->UnselectedConstant, Factory->UnselectedPainter,
				LOCTEXT("UnselectedBranchName", "Unselected"));
			if (!SelectedBranch || !UnselectedBranch)
			{
				return false;
			}
			return SelectedBranch->Initialize(InPainterContext) && UnselectedBranch->Initialize(InPainterContext);
		}

		virtual bool Prepare(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			using namespace UE::Geometry;

			if (!SelectedBranch->Prepare(InPainterContext) || !UnselectedBranch->Prepare(InPainterContext))
			{
				return false;
			}

			if (!InPainterContext.Mesh || !InPainterContext.MeshData)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoCanonicalView", "Selection Painter Switch needs a canonical Dynamic Mesh (with a DynMesh view) to evaluate its Selector."),
					Context);
				return false;
			}

			// Evaluate the Value Selection once, in the vertex domain, over the COMPLETE canonical mesh. The
			// DynMesh Selector infrastructure converts any native selector domain (vertex/edge/face) and any
			// composite Selection Logic to vertices here.
			FPCGUtilsDynMeshSelectionDomain Domain;
			Domain.ElementType = EGeometryElementType::Vertex;
			Domain.TopologyType = EGeometryTopologyType::Triangle;
			const FPCGUtilsDynMeshSelectionEvaluationContext SelectionContext(
				InPainterContext.MeshData, *InPainterContext.Mesh, Domain);

			FGeometrySelection VertexSelection;
			if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(
				Factory->Selector, SelectionContext, Context, VertexSelection))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("SelectorEvalFailed", "Selection Painter Switch could not evaluate its Selector '{0}' in the vertex domain."),
					FText::FromString(Factory->Selector->GetClass()->GetName())), Context);
				return false;
			}

			SelectedVertices.Init(false, InPainterContext.Mesh->MaxVertexID());
			for (const uint64 Encoded : VertexSelection.Selection)
			{
				const int32 VertexID = static_cast<int32>(FGeoSelectionID(Encoded).GeometryID);
				if (SelectedVertices.IsValidIndex(VertexID))
				{
					SelectedVertices[VertexID] = true;
				}
			}
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return (SelectedBranch->GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color
				|| UnselectedBranch->GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color)
				? EPCGUtilsDynMeshPainterValueType::Color
				: EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const bool bSelected =
				SelectedVertices.IsValidIndex(Sample.VertexID) && SelectedVertices[Sample.VertexID];
			// Only the active branch is evaluated — no evaluate-both-and-lerp.
			return bSelected ? SelectedBranch->Evaluate(Sample) : UnselectedBranch->Evaluate(Sample);
		}

	private:
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> MakeBranchOperation(
			EPCGUtilsPainterBranchSource Source, float Constant,
			const UPCGUtilsDynMeshPainterFactoryData* BranchPainter, const FText& BranchName)
		{
			if (Source == EPCGUtilsPainterBranchSource::Constant)
			{
				return MakeShared<FPCGUtilsPainterConstantOperation>(Constant);
			}
			if (!BranchPainter)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingBranchPainter", "Selection Painter Switch: the {0} branch is set to Painter but no Painter is connected."),
					BranchName), Context);
				return nullptr;
			}
			return BranchPainter->CreateOperation(Context);
		}

		TObjectPtr<const UPCGDynMeshSelectionPainterSwitchFactoryData> Factory;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> SelectedBranch;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> UnselectedBranch;
		TBitArray<> SelectedVertices;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshSelectionPainterSwitchFactoryData::CreateOperationInternal() const
{
	return MakeShared<FSelectionPainterSwitchOperation>(this);
}

void UPCGDynMeshSelectionPainterSwitchFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}
	uint32 SelectorCrc = Selector ? Selector->GetOrComputeCrc(true).GetValue() : 0;
	uint8 SelSource = static_cast<uint8>(SelectedSource);
	uint8 UnselSource = static_cast<uint8>(UnselectedSource);
	float SelConst = SelectedConstant;
	float UnselConst = UnselectedConstant;
	uint32 SelPainterCrc = SelectedPainter ? SelectedPainter->GetOrComputeCrc(true).GetValue() : 0;
	uint32 UnselPainterCrc = UnselectedPainter ? UnselectedPainter->GetOrComputeCrc(true).GetValue() : 0;
	Ar << SelectorCrc << SelSource << UnselSource << SelConst << UnselConst << SelPainterCrc << UnselPainterCrc;
}

// --- Shared provider base --------------------------------------------------------------------------------

FName UPCGDynMeshSelectionPainterSwitchProviderSettingsBase::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshSelectionPainterSwitchProviderSettingsBase::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshSelectionPainterSwitchProviderSettingsBase::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(SelectorPin, FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false)
		.SetRequiredPin();
	if (ExposesPainterBranchPins())
	{
		Pins.Emplace(SelectedPainterPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false);
		Pins.Emplace(UnselectedPainterPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false);
	}
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshSelectionPainterSwitchProviderSettingsBase::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> Selectors;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, SelectorPin, Selectors, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes(), true))
	{
		return nullptr;
	}
	if (Selectors.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("OneSelector", "Selection Painter Switch accepts exactly one Selector."), InContext);
		return nullptr;
	}

	const UPCGUtilsDynMeshPainterFactoryData* SelectedPainter = nullptr;
	const UPCGUtilsDynMeshPainterFactoryData* UnselectedPainter = nullptr;
	if (ExposesPainterBranchPins())
	{
		if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, SelectedPainterPin, SelectedPainter, false)
			|| !PCGUtilsDynMeshPainterFactories::GetSinglePainter(InContext, UnselectedPainterPin, UnselectedPainter, false))
		{
			return nullptr;
		}
	}

	UPCGDynMeshSelectionPainterSwitchFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshSelectionPainterSwitchFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshSelectionPainterSwitchFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Selector = Selectors[0];
	Factory->SelectedPainter = SelectedPainter;
	Factory->UnselectedPainter = UnselectedPainter;
	ConfigureBranches(*Factory);

	if (Factory->SelectedSource == EPCGUtilsPainterBranchSource::Painter && !Factory->SelectedPainter)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("SelectedNeedsPainter", "Selection Painter Switch: the Selected branch is set to Painter but no Painter is connected to 'Selected Painter'."),
			InContext);
		return nullptr;
	}
	if (Factory->UnselectedSource == EPCGUtilsPainterBranchSource::Painter && !Factory->UnselectedPainter)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("UnselectedNeedsPainter", "Selection Painter Switch: the Unselected branch is set to Painter but no Painter is connected to 'Unselected Painter'."),
			InContext);
		return nullptr;
	}

	return Super::CreateFactory(InContext, Factory);
}

// --- Selection to Painter (constant mask) --------------------------------------------------------------

#if WITH_EDITOR
FText UPCGDynMeshSelectionToPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("SelToPainterTitle", "Painter|From Selection");
}

FText UPCGDynMeshSelectionToPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("SelToPainterTooltip",
		"Converts a DynMesh selection into a scalar mask Painter: selected vertices return Selected Value (default 1), the rest return Unselected Value (default 0). This is Selection Painter Switch with both branches fixed to constants.");
}
#endif

void UPCGDynMeshSelectionToPainterProviderSettings::ConfigureBranches(
	UPCGDynMeshSelectionPainterSwitchFactoryData& Factory) const
{
	Factory.SelectedSource = EPCGUtilsPainterBranchSource::Constant;
	Factory.SelectedConstant = SelectedValue;
	Factory.UnselectedSource = EPCGUtilsPainterBranchSource::Constant;
	Factory.UnselectedConstant = UnselectedValue;
}

// --- Selection Painter Switch (full multiplexer) -----------------------------------------------------

#if WITH_EDITOR
FText UPCGDynMeshSelectionPainterSwitchProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("SwitchTitle", "Painter|Selection Switch");
}

FText UPCGDynMeshSelectionPainterSwitchProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("SwitchTooltip",
		"Binary Painter multiplexer driven by a DynMesh Selector. Each vertex takes the Selected branch or the Unselected branch; each branch is an independent constant or Painter. Only the chosen branch is evaluated per vertex. The Selector classifies the whole canonical mesh and is independent of the outer paint Write Selection.");
}
#endif

void UPCGDynMeshSelectionPainterSwitchProviderSettings::ConfigureBranches(
	UPCGDynMeshSelectionPainterSwitchFactoryData& Factory) const
{
	Factory.SelectedSource = SelectedSource;
	Factory.SelectedConstant = SelectedValue;
	Factory.UnselectedSource = UnselectedSource;
	Factory.UnselectedConstant = UnselectedValue;
}

#undef LOCTEXT_NAMESPACE
