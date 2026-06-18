#pragma once

#include <functional>

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

	struct ModalOverlayOptions
	{
		std::function<bool()> visible;
	};

	class ModalOverlay
	{
	public:
		static ftxui::Component Create(ftxui::Component modal, ModalOverlayOptions options);
	};

} // namespace codeharness::tui
