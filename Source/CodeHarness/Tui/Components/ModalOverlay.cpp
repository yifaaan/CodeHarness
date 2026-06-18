#include "Tui/Components/ModalOverlay.h"

#include <memory>
#include <utility>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace codeharness::tui
{
	namespace
	{
		class ModalOverlayComponent : public ftxui::ComponentBase
		{
		public:
			ModalOverlayComponent(ftxui::Component modal, ModalOverlayOptions options)
				: modal(std::move(modal)),
				  options(std::move(options))
			{
				Add(this->modal);
			}

			bool Focusable() const override
			{
				return options.visible && options.visible() && modal && modal->Focusable();
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (!options.visible || !options.visible() || !modal)
				{
					return false;
				}
				return modal->OnEvent(event);
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				if (!options.visible || !options.visible() || !modal)
				{
					return text("");
				}

				auto backdrop = filler() | bgcolor(Color::Black) | dim;
				auto modalElement = modal->Render() | bgcolor(Color::Black) | clear_under | center | vcenter;
				return dbox({
					std::move(backdrop),
					std::move(modalElement),
				});
			}

		private:
			ftxui::Component modal;
			ModalOverlayOptions options;
		};
	} // namespace

	ftxui::Component ModalOverlay::Create(ftxui::Component modal, ModalOverlayOptions options)
	{
		return std::make_shared<ModalOverlayComponent>(std::move(modal), std::move(options));
	}

} // namespace codeharness::tui
