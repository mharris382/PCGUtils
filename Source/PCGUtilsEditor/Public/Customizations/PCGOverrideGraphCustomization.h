#pragma once

#include "CoreMinimal.h"
#include "PropertyCustomizationHelpers.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

struct FOverrideGraphControlHandles
{
	FText ParentDisplayName;
	FText ParentTooltip ;
	TSharedPtr<IPropertyHandle> EnabledHandle;
	TSharedPtr<IPropertyHandle> GraphHandle;
	
	bool IsEnabled() const
	{
		if (EnabledHandle && EnabledHandle->IsValidHandle())
		{
			bool bEnabled = false;
			EnabledHandle->GetValue(bEnabled);
			return bEnabled;
		}
		return false;
	}
	
	bool IsValid() const
	{
		return EnabledHandle && EnabledHandle->IsValidHandle() && GraphHandle && GraphHandle->IsValidHandle();
	}
};

class FPCGOverrideGraphCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	
	
private:
	
	TSharedRef<SWidget> CreateAssetSelectorWidget(const TSharedRef<IPropertyHandle>& StructHandle, FDetailWidgetRow& HeaderRow, const FOverrideGraphControlHandles& ControlHandles);
	
	TSharedRef<SWidget> CreateHeaderNameWidget(const TSharedRef<IPropertyHandle>& PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		const FOverrideGraphControlHandles& ControlHandles);


	TSharedRef<SWidget> CreateHeaderValueWidget(const  TSharedRef<IPropertyHandle>& PropertyHandle,
		FDetailWidgetRow& HeaderRow, const FOverrideGraphControlHandles& ControlHandles);
	
	static TSharedRef<SWidget> GetPropertyWidgetSafe(const TSharedPtr<IPropertyHandle>& PropertyHandle) 
	{
		return PropertyHandle.IsValid() ? PropertyHandle->CreatePropertyValueWidget() : SNullWidget::NullWidget;
	}
};

