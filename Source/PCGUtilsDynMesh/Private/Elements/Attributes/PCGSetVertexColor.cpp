#include "Elements/Attributes/PCGSetVertexColor.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshVertexColorFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGMetadataDomain.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSetVertexColor"

namespace
{
	const FName SetVertexColorMeshPin = TEXT("Mesh");

	bool TryGetDataAttributeColor(const UPCGDynamicMeshData* SourceMeshData, FName AttributeName,
		FLinearColor& OutColor, FPCGContext* Context)
	{
		const FPCGMetadataDomain* DataDomain = (SourceMeshData && SourceMeshData->ConstMetadata())
			? SourceMeshData->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttributeBase* Attribute = DataDomain ? DataDomain->GetConstAttribute(AttributeName) : nullptr;
		if (!Attribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingColorAttribute", "Set Vertex Color could not find Data domain attribute '{0}'."),
				FText::FromName(AttributeName)), Context);
			return false;
		}

		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<FVector4>::Id)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("WrongColorAttributeType", "Set Vertex Color found Data domain attribute '{0}', but it is not a Vector4 (color) attribute."),
				FText::FromName(AttributeName)), Context);
			return false;
		}

		const FVector4 RawColor = static_cast<const FPCGMetadataAttribute<FVector4>*>(Attribute)->GetValueFromItemKey(PCGFirstEntryKey);
		OutColor = FLinearColor(RawColor.X, RawColor.Y, RawColor.Z, RawColor.W);
		return true;
	}
}

#if WITH_EDITOR
FText UPCGSetVertexColorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Set Vertex Color");
}

FText UPCGSetVertexColorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Sets a constant color (or a color read from a Data domain attribute) on a Dynamic Mesh's vertex colors, or on a Mesh Selection's elements.");
}
#endif

#if WITH_EDITOR
EPCGChangeType UPCGSetVertexColorSettings::GetChangeTypeForProperty(
	FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(UPCGSetVertexColorSettings, bUseDataAttributeColor))
	{
		// Toggling attribute mode changes whether this node accepts a Builder, so its pins must rebuild.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FName UPCGSetVertexColorSettings::GetMainInputPinLabel() const
{
	return SetVertexColorMeshPin;
}

FPCGElementPtr UPCGSetVertexColorSettings::CreateElement() const
{
	return MakeShared<FPCGSetVertexColorElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGSetVertexColorSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshSetVertexColorOperation> Operation =
		MakeShared<FPCGUtilsDynMeshSetVertexColorOperation>();
	Operation->ColorFlags = ColorFlags;
	Operation->bClearExisting = bClearExisting;
	Operation->bCreateColorSeam = bCreateColorSeam;
	Operation->bUseDataAttributeColor = bUseDataAttributeColor;
	Operation->Color = Color;
	Operation->ColorAttributeName = ColorAttributeName;
	return Operation;
}

bool FPCGUtilsDynMeshSetVertexColorOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	UDynamicMesh* TargetMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
	if (!TargetMesh)
	{
		return false;
	}

	// Colours are a per-vertex overlay; nothing about the topology changes.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	FLinearColor EffectiveColor = Color;
	if (bUseDataAttributeColor && !TryGetDataAttributeColor(
		Invocation.SourceMeshData, ColorAttributeName, EffectiveColor, Invocation.Context))
	{
		return false;
	}

	if (Invocation.SelectionData)
	{
		FGeometryScriptMeshSelection Selection;
		Selection.SetSelection(Invocation.SelectionData->GetSelection());
		UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshSelectionVertexColor(
			TargetMesh, Selection, EffectiveColor, ColorFlags, bCreateColorSeam);
	}
	else
	{
		UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshConstantVertexColor(
			TargetMesh, EffectiveColor, ColorFlags, bClearExisting);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
