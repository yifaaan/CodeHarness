#include <unknwn.h>
#include <Windows.h>
#include <MddBootstrap.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

#include "App.xaml.h"

namespace
{

	bool InitializeWindowsAppSdk()
	{
		PACKAGE_VERSION minVersion{};
		auto hr = MddBootstrapInitialize2(
			CODEHARNESS_WINDOWS_APP_SDK_MAJOR_MINOR,
			nullptr,
			minVersion,
			MddBootstrapInitializeOptions_OnNoMatch_ShowUI);
		if (FAILED(hr))
		{
			MessageBoxW(nullptr,
						L"CodeHarness Desktop requires the Windows App SDK 2.x runtime. Install the matching runtime and try again.",
						L"CodeHarness Desktop",
						MB_ICONERROR | MB_OK);
			return false;
		}
		return true;
	}

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	winrt::init_apartment(winrt::apartment_type::single_threaded);
	if (!InitializeWindowsAppSdk())
	{
		return 1;
	}

	winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
		winrt::make<winrt::CodeHarness::Desktop::implementation::App>();
	});

	MddBootstrapShutdown();
	return 0;
}
