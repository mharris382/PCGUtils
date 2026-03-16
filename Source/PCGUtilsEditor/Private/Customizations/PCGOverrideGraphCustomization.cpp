#include "Customizations/PCGOverrideGraphCustomization.h"

#include "OverrideGraphs.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "PCGOverrideGraphCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGOverrideGraphCustomization::MakeInstance()
{
    return MakeShared<FPCGOverrideGraphCustomization>();
}

void FPCGOverrideGraphCustomization::CustomizeHeader(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    FDetailWidgetRow& HeaderRow,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    FOverrideGraphControlHandles Handles;
    Handles.EnabledHandle    = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGOverrideGraph, bUseGraph));
    Handles.GraphHandle      = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGOverrideGraph, Graph));
    Handles.ParentDisplayName = StructPropertyHandle->GetPropertyDisplayName();
    Handles.ParentTooltip    = StructPropertyHandle->GetToolTipText();

    if (!Handles.IsValid())
    {
        HeaderRow
            .NameContent()  [ StructPropertyHandle->CreatePropertyNameWidget() ]
            .ValueContent() [ StructPropertyHandle->CreatePropertyValueWidget() ];
        return;
    }

    HeaderRow
        .NameContent()
        [ CreateHeaderNameWidget(StructPropertyHandle, HeaderRow, Handles) ]
        .ValueContent()
        .MinDesiredWidth(250.f)
        [ CreateHeaderValueWidget(StructPropertyHandle, HeaderRow, Handles) ];
}

void FPCGOverrideGraphCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle> StructPropertyHandle,
    IDetailChildrenBuilder& ChildBuilder,
    IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
    // Intentionally empty — both fields are surfaced in the header row.
}

TSharedRef<SWidget> FPCGOverrideGraphCustomization::CreateHeaderNameWidget(
    const TSharedRef<IPropertyHandle>& StructHandle,
    FDetailWidgetRow& HeaderRow,
    const FOverrideGraphControlHandles& ControlHandles)
{
    return SNew(SHorizontalBox)

        // Toggle checkbox
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.f, 0.f, 4.f, 0.f)
        [
            GetPropertyWidgetSafe(ControlHandles.EnabledHandle)
        ]

        // Parent property name as label e.g. "PostSpawnGraph"
        + SHorizontalBox::Slot()
        .VAlign(VAlign_Center)
        .FillWidth(1.f)
        [
            SNew(STextBlock)
            .Text(ControlHandles.ParentDisplayName)
            .ToolTipText(ControlHandles.ParentTooltip)
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ];
}

TSharedRef<SWidget> FPCGOverrideGraphCustomization::CreateHeaderValueWidget(
    const TSharedRef<IPropertyHandle>& StructHandle,
    FDetailWidgetRow& HeaderRow,
    const FOverrideGraphControlHandles& ControlHandles)
{
    return SNew(SBox)
        .IsEnabled_Lambda([ControlHandles]()
        {
            return ControlHandles.IsEnabled();
        })
        [
            CreateAssetSelectorWidget(StructHandle, HeaderRow, ControlHandles)
        ];
}

TSharedRef<SWidget> FPCGOverrideGraphCustomization::CreateAssetSelectorWidget(
    const TSharedRef<IPropertyHandle>& StructHandle,
    FDetailWidgetRow& HeaderRow,
    const FOverrideGraphControlHandles& ControlHandles)
{
    return GetPropertyWidgetSafe(ControlHandles.GraphHandle);
}

#undef LOCTEXT_NAMESPACE