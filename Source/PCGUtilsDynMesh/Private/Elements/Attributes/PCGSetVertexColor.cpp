#include "Elements/Attributes/PCGSetVertexColor.h"

#include "Data/PCGDynamicMeshData.h"
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

	void SetVertexColorOne(FPCGContext* Context, const UPCGSetVertexColorSettings* Settings, const FPCGTaggedData& Input)
	{
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
			Input.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context, Settings);
		if (!Handle.IsValid())
		{
			return;
		}

		FLinearColor Color = Settings->Color;
		if (Settings->bUseDataAttributeColor
			&& !TryGetDataAttributeColor(Handle.GetSourceMeshData(), Settings->ColorAttributeName, Color, Context))
		{
			return;
		}

		if (!Handle.IsEmptySelectionNoOp())
		{
			if (Handle.IsSelection())
			{
				UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshSelectionVertexColor(
					Handle.GetTargetMesh(), Handle.GetSelection(), Color, Settings->ColorFlags, Settings->bCreateColorSeam);
			}
			else
			{
				UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshConstantVertexColor(
					Handle.GetTargetMesh(), Color, Settings->ColorFlags, Settings->bClearExisting);
			}
		}

		FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
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

TArray<FPCGPinProperties> UPCGSetVertexColorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins[0] = FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SetVertexColorMeshPin);
	Pins[0].SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSetVertexColorSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGSetVertexColorSettings::CreateElement() const
{
	return MakeShared<FPCGSetVertexColorElement>();
}

bool FPCGSetVertexColorElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);

	const UPCGSetVertexColorSettings* Settings = Context->GetInputSettings<UPCGSetVertexColorSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SetVertexColorMeshPin))
	{
		SetVertexColorOne(Context, Settings, Input);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
