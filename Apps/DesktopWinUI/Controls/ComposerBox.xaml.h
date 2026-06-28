#pragma once

#include <unknwn.h>

#include "Controls.ComposerBox.g.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct ComposerBox : ComposerBoxT<ComposerBox>
	{
		ComposerBox();

		// IDL-projected surface.
		void SetRunning(bool running);
		void FocusPrompt();
		void InsertPromptText(winrt::hstring text);
		void AddContextAttachment(winrt::hstring path, bool directory);
		winrt::hstring SelectedModel();
		void SetSelectedModel(winrt::hstring model);
		bool IsThinkingEnabled();

		// C++-only: populate the model picker from the backend (called by ShellPage).
		void SetAvailableModels(std::vector<std::wstring> models);

		// C++-only callbacks (sibling XAML pages set these directly).
		void OnSubmit(std::function<void(std::wstring)> cb) { m_onSubmit = std::move(cb); }
		void OnCancel(std::function<void()> cb) { m_onCancel = std::move(cb); }
		void OnModelSelected(std::function<void(std::wstring)> cb) { m_onModelSelected = std::move(cb); }
		void OnThinkingToggled(std::function<void(bool)> cb) { m_onThinkingToggled = std::move(cb); }

		// XAML-wired event handlers (public for the generated template base).
		void OnPromptKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
		                     winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
		void OnSendClick(winrt::Windows::Foundation::IInspectable const& sender,
		                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnCancelClick(winrt::Windows::Foundation::IInspectable const& sender,
		                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnActionMenuClick(winrt::Windows::Foundation::IInspectable const& sender,
		                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnModeSelectorClick(winrt::Windows::Foundation::IInspectable const& sender,
		                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnModelSelectorClick(winrt::Windows::Foundation::IInspectable const& sender,
		                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnPermissionSelectorClick(winrt::Windows::Foundation::IInspectable const& sender,
		                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnThinkingClick(winrt::Windows::Foundation::IInspectable const& sender,
		                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

	private:
		struct ContextAttachment
		{
			std::wstring path;
			bool directory = false;
		};

		void SubmitCurrent();
		void InsertPromptToken(std::wstring_view token);
		void RemoveContextAttachment(std::wstring const& path, bool directory);
		void RebuildAttachmentChips();
		std::wstring BuildAttachmentPrefix() const;

		std::function<void(std::wstring)> m_onSubmit;
		std::function<void()> m_onCancel;
		std::function<void(std::wstring)> m_onModelSelected;
		std::function<void(bool)> m_onThinkingToggled;
		bool m_running = false;
		bool m_thinkingEnabled = false;
		std::wstring m_selectedModel{ L"GLM 5.2" };
		std::vector<ContextAttachment> m_contextAttachments;
		// Model list shown in the picker. Defaults to a sensible placeholder until
		// ShellPage injects the real list from the backend.
		std::vector<std::wstring> m_models{ L"GLM 5.2", L"GLM 4.6", L"GLM 4.5" };
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct ComposerBox : ComposerBoxT<ComposerBox, implementation::ComposerBox>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
