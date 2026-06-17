#pragma once

#include <unknwn.h>

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include "XamlMetaDataProvider.g.h"

namespace winrt::CodeHarness::Desktop::implementation
{

	struct XamlMetaDataProvider : XamlMetaDataProviderT<XamlMetaDataProvider>
	{
		XamlMetaDataProvider() = default;

		Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type);
		Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(hstring const& fullName);
		com_array<Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions();
	};

} // namespace winrt::CodeHarness::Desktop::implementation

namespace winrt::CodeHarness::Desktop::factory_implementation
{

	struct XamlMetaDataProvider : XamlMetaDataProviderT<XamlMetaDataProvider, implementation::XamlMetaDataProvider>
	{
	};

} // namespace winrt::CodeHarness::Desktop::factory_implementation
