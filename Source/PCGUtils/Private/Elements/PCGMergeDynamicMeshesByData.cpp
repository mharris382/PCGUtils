#include "Elements/PCGMergeDynamicMeshesByData.h"

#include "PCGContext.h"
#include "Data/PCGDynamicMeshData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Utils/PCGLogErrors.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInterface.h"
#include "DynamicMeshEditor.h"
#include "UDynamicMesh.h"
#include "Helpers/PCGGeometryHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMergeDynamicMeshesByData)

#define LOCTEXT_NAMESPACE "PCGMergeDynamicMeshesByDataElement"

// Sentinel key used for meshes with no attribute or a null path value.
static const FSoftObjectPath NullGroupKey = FSoftObjectPath();

namespace 
{
    static void RemapMaterialsLocal(
    UE::Geometry::FDynamicMesh3& InMesh,
    const TArray<TObjectPtr<UMaterialInterface>>& FromMaterials,
    TArray<TObjectPtr<UMaterialInterface>>& ToMaterials,
    const UE::Geometry::FMeshIndexMappings* OptionalMappings)
    {
        if (FromMaterials.IsEmpty() || !InMesh.HasAttributes() || !InMesh.Attributes()->HasMaterialID())
    {
        return;
    }

        // If we have no materials in output, we can just copy the in materials
        if (ToMaterials.IsEmpty())
        {
            ToMaterials = FromMaterials;
            return;
        }

        TMap<int32, int32> MaterialIDRemap;
        MaterialIDRemap.Reserve(FromMaterials.Num());
		
        for (int32 FromMaterialIndex = 0; FromMaterialIndex < FromMaterials.Num(); ++FromMaterialIndex)
        {
            UMaterialInterface* FromMaterial = FromMaterials[FromMaterialIndex];
            int32 ToMaterialIndex = ToMaterials.IndexOfByKey(FromMaterial);
            if (ToMaterialIndex == INDEX_NONE)
            {
                ToMaterialIndex = ToMaterials.Add(FromMaterial);
            }

            if (ToMaterialIndex != FromMaterialIndex)
            {
                MaterialIDRemap.Emplace(FromMaterialIndex, ToMaterialIndex);
            }
        }

        if (!MaterialIDRemap.IsEmpty())
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(PCGGeometryHelpers::RemapMaterials);
            UE::Geometry::FDynamicMeshMaterialAttribute* MaterialAttribute = InMesh.Attributes()->GetMaterialID();

            auto Remap = [&MaterialAttribute, &MaterialIDRemap](const int32 TriangleID)
            {
                const int32 OriginalMaterialID = MaterialAttribute->GetValue(TriangleID);
                if (const int32* RemappedMaterialID = MaterialIDRemap.Find(OriginalMaterialID))
                {
                    MaterialAttribute->SetValue(TriangleID, *RemappedMaterialID);
                }
            };
			
            // TODO: Could be parallelized
            if (OptionalMappings)
            {
                for (const TPair<int32, int32>& MapTriangleID : OptionalMappings->GetTriangleMap().GetForwardMap())
                {
                    Remap(MapTriangleID.Value);
                }
            }
            else
            {
                for (const int32 TriangleID : InMesh.TriangleIndicesItr())
                {
                    Remap(TriangleID);
                }
            }
        }
    }
}

#if WITH_EDITOR
FName UPCGMergeDynamicMeshesByDataSettings::GetDefaultNodeName() const
{
    return FName(TEXT("MergeDynamicMeshesByData"));
}

FText UPCGMergeDynamicMeshesByDataSettings::GetDefaultNodeTitle() const
{
    return LOCTEXT("NodeTitle", "Merge Dynamic Meshes By Data");
}

FText UPCGMergeDynamicMeshesByDataSettings::GetNodeTooltipText() const
{
    return LOCTEXT("NodeTooltip",
        "Groups incoming dynamic meshes by a shared @Data FSoftObjectPath attribute, "
        "merges each group into a single mesh, and outputs one mesh per unique value. "
        "Minimizes downstream loop iterations for override graph execution.");
}
#endif

FPCGElementPtr UPCGMergeDynamicMeshesByDataSettings::CreateElement() const
{
    return MakeShared<FPCGMergeDynamicMeshesByDataElement>();
}

TArray<FPCGPinProperties> UPCGMergeDynamicMeshesByDataSettings::OutputPinProperties() const
{
    TArray<FPCGPinProperties> Properties;
    Properties.Emplace(PCGPinConstants::DefaultOutputLabel,
        EPCGDataType::DynamicMesh, /*bAllowMultipleData=*/true, false);
    return Properties;
}

bool FPCGMergeDynamicMeshesByDataElement::ExecuteInternal(FPCGContext* Context) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMergeDynamicMeshesByDataElement::Execute);

    check(Context);

    const UPCGMergeDynamicMeshesByDataSettings* Settings =
        Context->GetInputSettings<UPCGMergeDynamicMeshesByDataSettings>();
    check(Settings);

    if (Settings->GroupByAttributeName == NAME_None)
    {
        PCGE_LOG(Warning, GraphAndLog,
            LOCTEXT("NoAttributeName",
                "MergeDynamicMeshesByData: GroupByAttributeName is not set. "
                "All meshes will be merged into a single null-key group."));
    }

    // ── Group inputs by @Data attribute value ─────────────────────────────────

    // Ordered list of unique keys — preserves first-seen order for determinism.
    TArray<FSoftObjectPath> GroupKeyOrder;

    // Key -> { first TaggedData (for tags), accumulated inputs }
    struct FMeshGroup
    {
        FPCGTaggedData          FirstTaggedData;
        TArray<const UPCGDynamicMeshData*> Meshes;
    };
    TMap<FSoftObjectPath, FMeshGroup> Groups;

    const FPCGAttributeIdentifier GroupAttrIdentifier(
        Settings->GroupByAttributeName,
        PCGMetadataDomainID::Data);

    for (const FPCGTaggedData& Input :
        Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
    {
        const UPCGDynamicMeshData* InputData = Cast<const UPCGDynamicMeshData>(Input.Data);
        if (!InputData)
        {
            PCGLog::InputOutput::LogInvalidInputDataError(Context);
            continue;
        }

        // Read the @Data attribute to determine the group key.
        FSoftObjectPath GroupKey = NullGroupKey;

        if (Settings->GroupByAttributeName != NAME_None)
        {
            const UPCGMetadata* Metadata = InputData->ConstMetadata();
            if (Metadata)
            {
                const FPCGMetadataAttribute<FSoftObjectPath>* Attribute =
                    Metadata->GetConstTypedAttribute<FSoftObjectPath>(GroupAttrIdentifier);

                if (Attribute)
                {
                    const FSoftObjectPath Path = Attribute->GetValue(PCGInvalidEntryKey);
                    if (!Path.IsNull())
                    {
                        GroupKey = Path;
                    }
                }
            }
        }

        FMeshGroup& Group = Groups.FindOrAdd(GroupKey);
        if (Group.Meshes.IsEmpty())
        {
            // First mesh in this group — record its TaggedData for tags and order.
            Group.FirstTaggedData = Input;
            GroupKeyOrder.AddUnique(GroupKey);
        }
        Group.Meshes.Add(InputData);
    }

    if (Groups.IsEmpty())
    {
        return true;
    }

    // ── Merge each group and emit output ─────────────────────────────────────

    for (const FSoftObjectPath& Key : GroupKeyOrder)
    {
        FMeshGroup& Group = Groups[Key];
        if (Group.Meshes.IsEmpty())
        {
            continue;
        }

        // Seed the output from the first mesh in the group.
        UPCGDynamicMeshData* OutputData = CopyOrSteal(Group.FirstTaggedData, Context);
        if (!OutputData)
        {
         // PCGE_LOG(Warning, GraphAndLog,
         //     LOCTEXT("CopyFailed",
         //         "MergeDynamicMeshesByData: CopyOrSteal failed for group seed mesh."));
            continue;
        }

        // Append remaining meshes in the group.
        for (int32 i = 1; i < Group.Meshes.Num(); ++i)
        {
            const UPCGDynamicMeshData* InputData = Group.Meshes[i];

            UE::Geometry::FMeshIndexMappings MeshIndexMappings;

            {
                TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMergeDynamicMeshesByDataElement::Execute::AppendMesh);
                UE::Geometry::FDynamicMeshEditor Editor(
                    OutputData->GetMutableDynamicMesh()->GetMeshPtr());
                Editor.AppendMesh(
                    InputData->GetDynamicMesh()->GetMeshPtr(),
                    MeshIndexMappings);
            }

            const TArray<TObjectPtr<UMaterialInterface>>& InputMaterials =
                InputData->GetMaterials();
            TArray<TObjectPtr<UMaterialInterface>>& OutputMaterials =
                OutputData->GetMutableMaterials();

            if (!InputMaterials.IsEmpty() && InputMaterials != OutputMaterials)
            {
                RemapMaterialsLocal(
                    OutputData->GetMutableDynamicMesh()->GetMeshRef(),InputMaterials,OutputMaterials, &MeshIndexMappings);
            }
        }

        // Stamp the group key back onto the merged output @Data so downstream
        // consumers (e.g. override graph invokers) can read which graph to execute.
        if (!Key.IsNull())
        {
            FPCGMetadataDomain* DataDomain =
                OutputData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);

            if (DataDomain)
            {
                if (FPCGMetadataAttribute<FSoftObjectPath>* Attr =
                    DataDomain->FindOrCreateAttribute<FSoftObjectPath>(
                        Settings->GroupByAttributeName, FSoftObjectPath(),
                        /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false,
                        /*bOverwriteIfTypeMismatch=*/false))
                {
                    Attr->SetValue(PCGInvalidEntryKey, Key);
                }
            }
        }

        // Emit — preserve tags from the first mesh in the group.
        FPCGTaggedData& Output =
            Context->OutputData.TaggedData.Emplace_GetRef(Group.FirstTaggedData);
        Output.Data = OutputData;
    }

    return true;
}

#undef LOCTEXT_NAMESPACE
