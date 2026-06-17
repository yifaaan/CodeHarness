#include "XamlMetaDataProvider.h"

#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

#include "XamlMetaDataProvider.g.cpp"

namespace winrt::CodeHarness::Desktop::implementation
{

	Microsoft::UI::Xaml::Markup::IXamlType XamlMetaDataProvider::GetXamlType(
		Windows::UI::Xaml::Interop::TypeName const& type)
	{
		auto provider = Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider{};
		return provider.GetXamlType(type);
	}

	Microsoft::UI::Xaml::Markup::IXamlType XamlMetaDataProvider::GetXamlType(hstring const& fullName)
	{
		auto provider = Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider{};
		return provider.GetXamlType(fullName);
	}

	com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition> XamlMetaDataProvider::GetXmlnsDefinitions()
	{
		auto provider = Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider{};
		return provider.GetXmlnsDefinitions();
	}

} // namespace winrt::CodeHarness::Desktop::implementation
