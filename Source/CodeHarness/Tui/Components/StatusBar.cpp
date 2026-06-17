#include "Tui/Components/StatusBar.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Config/ConfigTypes.h"
#include "Llm/Types.h"
#include "Tui/TuiState.h"

namespace codeharness::tui
{

	namespace
	{

		int64_t TotalTokens(const llm::TokenUsage& usage)
		{
			return usage.inputOther + usage.output + usage.inputCacheRead + usage.inputCacheCreation;
		}

		std::string ModeLabel(config::PermissionMode mode)
		{
			switch (mode)
			{
			case config::PermissionMode::Yolo:
				return "yolo";
			case config::PermissionMode::Auto:
				return "auto";
			case config::PermissionMode::Manual:
			default:
				return "manual";
			}
		}

		std::string ShortenWorkdir(const std::string& path)
		{
			if (path.empty())
			{
				return "-";
			}

			constexpr char slash = '\\';
			auto last = path.find_last_of("\\/");
			if (last == std::string::npos || last + 1 >= path.size())
			{
				return path;
			}
			auto prev = path.find_last_of("\\/", last - 1);
			if (prev == std::string::npos)
			{
				return path.substr(last + 1);
			}
			return std::string("…") + slash + path.substr(prev + 1);
		}

		ftxui::Element ModeBadge(config::PermissionMode mode)
		{
			using namespace ftxui;
			auto colorValue = mode == config::PermissionMode::Manual ? Color::GrayLight : Color::Yellow;
			return text(fmt::format("[{}]", ModeLabel(mode))) | bold | color(colorValue);
		}

	} // namespace

	ftxui::Component StatusBar::Create(std::shared_ptr<TuiState> state)
	{
		using namespace ftxui;
		return Renderer([state = std::move(state)] {
			std::lock_guard<std::mutex> lk(state->mutex);

			const std::string model = state->model.empty() ? "no model" : state->model;
			const std::string workdir = ShortenWorkdir(state->workdir);
			const int64_t tokens = TotalTokens(state->lastUsage);
			const std::string context = tokens > 0 ? fmt::format("context: {} tokens", tokens) : "context: 0%";
			const std::string activity = state->streaming ? "◐ working" : "● ready";

			Elements left;
			left.push_back(text(" "));
			left.push_back(ModeBadge(state->permissionMode));
			left.push_back(text("  "));
			left.push_back(text(model) | bold | color(Color::Blue));
			left.push_back(text("  "));
			left.push_back(text(workdir) | dim | color(Color::GrayLight));

			Elements tips;
			tips.push_back(text("/help") | dim);
			tips.push_back(text("  ") | dim);
			tips.push_back(text("/model") | dim);
			tips.push_back(text("  ") | dim);
			tips.push_back(text("ctrl+o: expand") | dim);
			tips.push_back(text("  ") | dim);
			tips.push_back(text("ctrl+c: cancel") | dim);

			Color activityColor = state->streaming ? Color::Cyan : Color::Green;
			return vbox({
				separatorLight(),
				hbox({
					hbox(std::move(left)) | flex,
					hbox(std::move(tips)) | color(Color::GrayLight),
					text(" "),
				}),
					hbox({
						text(state->statusMessage.empty() ? " " : state->statusMessage) | dim | color(Color::GrayLight),
						text(" ") | flex,
						text(activity) | color(activityColor),
					text("  "),
					text(context) | color(Color::GrayLight),
					text(" "),
				}),
			});
		});
	}

} // namespace codeharness::tui
