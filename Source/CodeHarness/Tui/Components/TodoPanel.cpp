#include "Tui/Components/TodoPanel.h"

#include <mutex>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

namespace
{

using ftxui::Color;
using ftxui::Element;
using ftxui::Elements;
using ftxui::text;

Element RenderTodo(const TodoItem& todo)
{
	const char* marker;
	Color markColor;
	switch (todo.status)
	{
		case TodoItem::Status::Done:
			marker = "[x]";
			markColor = Color::Green;
			break;
		case TodoItem::Status::InProgress:
			marker = "[-]";
			markColor = Color::Yellow;
			break;
		case TodoItem::Status::Pending:
		default:
			marker = "[ ]";
			markColor = Color::GrayLight;
			break;
	}

	auto body = text(todo.text);
	if (todo.status == TodoItem::Status::Done)
		body = body | ftxui::dim;
	else if (todo.status == TodoItem::Status::InProgress)
		body = body | ftxui::bold;

	return ftxui::hbox({
		text("  "),
		text(marker) | ftxui::color(markColor),
		text(" "),
		std::move(body),
	});
}

} // namespace

ftxui::Component TodoPanel::Create(std::shared_ptr<TuiState> state)
{
	using namespace ftxui;
	return Renderer([state = std::move(state)] {
		std::lock_guard<std::mutex> lk(state->mutex);
		if (!state->todoPanelVisible)
		{
			return text("");
		}

		Elements rows;
		rows.push_back(text(" Tasks") | bold | color(Color::Cyan));
		if (state->todos.empty())
		{
			rows.push_back(text("  No active tasks") | dim | color(Color::GrayLight));
		}
		else
		{
			for (const auto& todo : state->todos)
			{
				rows.push_back(RenderTodo(todo));
			}
		}
		return vbox(std::move(rows)) | ftxui::borderLight;
	});
}

} // namespace codeharness::tui
