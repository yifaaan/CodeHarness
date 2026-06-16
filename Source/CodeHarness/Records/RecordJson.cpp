#include "Records/RecordJson.h"

#include <absl/status/status.h>
#include <fmt/format.h>

#include <string>
#include <string_view>

namespace codeharness::records
{

	namespace
	{

		constexpr std::string_view kProtocol = "1.0";

		const char* RoleToString(llm::Role role)
		{
			switch (role)
			{
			case llm::Role::User:
				return "user";
			case llm::Role::Assistant:
				return "assistant";
			case llm::Role::Tool:
				return "tool";
			}
			return "user";
		}

		absl::StatusOr<llm::Role> RoleFromString(std::string_view s)
		{
			if (s == "user")
				return llm::Role::User;
			if (s == "assistant")
				return llm::Role::Assistant;
			if (s == "tool")
				return llm::Role::Tool;
			return absl::InvalidArgumentError(fmt::format("unknown role: {}", s));
		}

	} // namespace

	nlohmann::json ContentPartToJson(const llm::ContentPart& part)
	{
		if (auto* text = std::get_if<llm::TextPart>(&part))
		{
			return {{"type", "text"}, {"text", text->text}};
		}
			if (auto* think = std::get_if<llm::ThinkPart>(&part))
			{
				nlohmann::json obj = {{"type", "think"}, {"think", think->think}};
				if (think->encrypted)
					obj["encrypted"] = *think->encrypted;
				return obj;
			}
			if (auto* image = std::get_if<llm::ImagePart>(&part))
			{
				nlohmann::json obj = {{"type", "image"}, {"url", image->url}};
				if (!image->mimeType.empty())
					obj["mime_type"] = image->mimeType;
				if (!image->detail.empty())
					obj["detail"] = image->detail;
				return obj;
			}
			if (auto* video = std::get_if<llm::VideoPart>(&part))
			{
				nlohmann::json obj = {{"type", "video"}, {"url", video->url}};
				if (!video->mimeType.empty())
					obj["mime_type"] = video->mimeType;
				return obj;
			}
			return {{"type", "text"}, {"text", ""}};
		}

	absl::StatusOr<llm::ContentPart> ContentPartFromJson(const nlohmann::json& j)
	{
		if (!j.is_object() || !j.contains("type"))
		{
			return absl::InvalidArgumentError("content part missing 'type'");
		}
		auto type = j["type"].get<std::string>();
		if (type == "text")
		{
			llm::TextPart p;
			if (j.contains("text"))
				p.text = j["text"].get<std::string>();
			return p;
		}
			if (type == "think")
			{
				llm::ThinkPart p;
				if (j.contains("think"))
					p.think = j["think"].get<std::string>();
			if (j.contains("encrypted"))
					p.encrypted = j["encrypted"].get<std::string>();
				return p;
			}
			if (type == "image")
			{
				llm::ImagePart p;
				p.url = j.value("url", "");
				p.mimeType = j.value("mime_type", "");
				p.detail = j.value("detail", "");
				return p;
			}
			if (type == "video")
			{
				llm::VideoPart p;
				p.url = j.value("url", "");
				p.mimeType = j.value("mime_type", "");
				return p;
			}
			return absl::InvalidArgumentError(fmt::format("unknown content part type: {}", type));
		}

	nlohmann::json MessageToJson(const llm::Message& msg)
	{
		nlohmann::json obj;
		obj["role"] = RoleToString(msg.role);

		auto content = nlohmann::json::array();
		for (const auto& part : msg.content)
		{
			content.push_back(ContentPartToJson(part));
		}
		obj["content"] = std::move(content);

		if (msg.toolCallId)
		{
			obj["tool_call_id"] = *msg.toolCallId;
		}

		if (!msg.toolCalls.empty())
		{
			auto calls = nlohmann::json::array();
			for (const auto& tc : msg.toolCalls)
			{
				calls.push_back({{"id", tc.id}, {"name", tc.name}, {"arguments", tc.arguments}});
			}
			obj["tool_calls"] = std::move(calls);
		}

		return obj;
	}

	absl::StatusOr<llm::Message> MessageFromJson(const nlohmann::json& j)
	{
		if (!j.is_object() || !j.contains("role"))
		{
			return absl::InvalidArgumentError("message missing 'role'");
		}
		llm::Message msg;
		auto roleResult = RoleFromString(j["role"].get<std::string>());
		if (!roleResult.ok())
			return roleResult.status();
		msg.role = *roleResult;

		if (j.contains("content") && j["content"].is_array())
		{
			for (const auto& part : j["content"])
			{
				auto partResult = ContentPartFromJson(part);
				if (!partResult.ok())
					return partResult.status();
				msg.content.push_back(*partResult);
			}
		}

		if (j.contains("tool_call_id"))
		{
			msg.toolCallId = j["tool_call_id"].get<std::string>();
		}

		if (j.contains("tool_calls") && j["tool_calls"].is_array())
		{
			for (const auto& tc : j["tool_calls"])
			{
				llm::ToolCall call;
				call.id = tc.value("id", "");
				call.name = tc.value("name", "");
				call.arguments = tc.value("arguments", "");
				msg.toolCalls.push_back(std::move(call));
			}
		}

		return msg;
	}

	nlohmann::json LoopEventToJson(const engine::LoopEvent& event)
	{
		nlohmann::json obj;
		std::visit(
			[&](const auto& e) {
				using T = std::decay_t<decltype(e)>;
				if constexpr (std::is_same_v<T, engine::StepStartedEvent>)
					obj["StepStarted"] = {{"step", e.step}};
				else if constexpr (std::is_same_v<T, engine::StepCompletedEvent>)
					obj["StepCompleted"] = {{"step", e.step}};
				else if constexpr (std::is_same_v<T, engine::AssistantDeltaEvent>)
					obj["AssistantDelta"] = {{"text", e.text}};
				else if constexpr (std::is_same_v<T, engine::ToolCallStartedEvent>)
				{
					nlohmann::json inner = {{"id", e.id}, {"name", e.name}};
					inner["args"] = e.args;
					obj["ToolCallStarted"] = std::move(inner);
				}
				else if constexpr (std::is_same_v<T, engine::ToolResultEvent>)
				{
					nlohmann::json inner = {{"id", e.id}, {"name", e.name}};
					inner["result"] = {{"isError", e.result.isError}, {"content", e.result.content}};
					obj["ToolResult"] = std::move(inner);
				}
				else if constexpr (std::is_same_v<T, engine::PermissionRequestedEvent>)
				{
					nlohmann::json inner = {{"name", e.toolName}, {"description", e.description}};
					inner["args"] = e.args;
					obj["PermissionRequested"] = std::move(inner);
				}
				else if constexpr (std::is_same_v<T, engine::PermissionDeniedEvent>)
				{
					obj["PermissionDenied"] = {{"name", e.toolName}, {"description", e.description}};
				}
				else if constexpr (std::is_same_v<T, engine::ErrorEvent>)
					obj["Error"] = {{"message", e.message}};
			},
			event);
		return obj;
	}

	absl::StatusOr<engine::LoopEvent> LoopEventFromJson(const nlohmann::json& j)
	{
		if (!j.is_object() || j.empty())
		{
			return absl::InvalidArgumentError("loop event object is empty");
		}

		for (auto it = j.begin(); it != j.end(); ++it)
		{
			const auto& key = it.key();
			const auto& inner = it.value();

			if (key == "StepStarted")
			{
				engine::StepStartedEvent e;
				e.step = inner.value("step", 0);
				return e;
			}
			if (key == "StepCompleted")
			{
				engine::StepCompletedEvent e;
				e.step = inner.value("step", 0);
				return e;
			}
			if (key == "AssistantDelta")
			{
				engine::AssistantDeltaEvent e;
				e.text = inner.value("text", "");
				return e;
			}
			if (key == "ToolCallStarted")
			{
				engine::ToolCallStartedEvent e;
				e.id = inner.value("id", "");
				e.name = inner.value("name", "");
				if (inner.contains("args"))
					e.args = inner["args"];
				return e;
			}
			if (key == "ToolResult")
			{
				engine::ToolResultEvent e;
				e.id = inner.value("id", "");
				e.name = inner.value("name", "");
				if (inner.contains("result") && inner["result"].is_object())
				{
					e.result.isError = inner["result"].value("isError", false);
					e.result.content = inner["result"].value("content", "");
				}
				return e;
			}
			if (key == "PermissionRequested")
			{
				engine::PermissionRequestedEvent e;
				e.toolName = inner.value("name", "");
				e.description = inner.value("description", "");
				if (inner.contains("args"))
					e.args = inner["args"];
				return e;
			}
			if (key == "PermissionDenied")
			{
				engine::PermissionDeniedEvent e;
				e.toolName = inner.value("name", "");
				e.description = inner.value("description", "");
				return e;
			}
			if (key == "Error")
			{
				engine::ErrorEvent e;
				e.message = inner.value("message", "");
				return e;
			}
			return absl::InvalidArgumentError(fmt::format("unknown loop event kind: {}", key));
		}
		return absl::InvalidArgumentError("loop event object is empty");
	}

	nlohmann::json WireRecordToJson(const WireRecord& wire)
	{
		nlohmann::json obj;
		obj["protocol"] = wire.meta.protocol.empty() ? std::string(kProtocol) : wire.meta.protocol;
		obj["ts"] = wire.meta.ts;

		std::visit(
			[&](const auto& r) {
				using T = std::decay_t<decltype(r)>;
				if constexpr (std::is_same_v<T, TurnPromptRecord>)
				{
					obj["type"] = "turn.prompt";
					obj["turnId"] = r.turnId;
					auto input = nlohmann::json::array();
					for (const auto& part : r.input)
					{
						input.push_back(ContentPartToJson(part));
					}
					obj["input"] = std::move(input);
					obj["origin"] = r.origin == 1 ? "system_trigger" : "user";
				}
				else if constexpr (std::is_same_v<T, TurnCancelRecord>)
				{
					obj["type"] = "turn.cancel";
					obj["turnId"] = r.turnId;
				}
				else if constexpr (std::is_same_v<T, ContextAppendMessageRecord>)
				{
					obj["type"] = "context.append_message";
					obj["message"] = MessageToJson(r.message);
				}
				else if constexpr (std::is_same_v<T, ContextAppendLoopEventRecord>)
				{
					obj["type"] = "context.append_loop_event";
					obj["event"] = LoopEventToJson(r.event);
				}
			},
			wire.record);

		return obj;
	}

	absl::StatusOr<WireRecord> ParseWireRecord(const std::string& line)
	{
		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(line);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			return absl::InvalidArgumentError(fmt::format("json parse error: {}", e.what()));
		}

		if (!j.is_object() || !j.contains("type"))
		{
			return absl::InvalidArgumentError("record missing 'type'");
		}

		WireRecord wire;
		wire.meta.protocol = j.value("protocol", std::string(kProtocol));
		wire.meta.ts = j.value("ts", static_cast<std::int64_t>(0));

		auto type = j["type"].get<std::string>();

		if (type == "turn.prompt")
		{
			TurnPromptRecord r;
			r.turnId = j.value("turnId", "");
			if (j.contains("input") && j["input"].is_array())
			{
				for (const auto& part : j["input"])
				{
					auto p = ContentPartFromJson(part);
					if (!p.ok())
						return p.status();
					r.input.push_back(*p);
				}
			}
			auto origin = j.value("origin", "user");
			r.origin = (origin == "system_trigger") ? 1 : 0;
			wire.record = std::move(r);
			return wire;
		}

		if (type == "turn.cancel")
		{
			TurnCancelRecord r;
			r.turnId = j.value("turnId", "");
			wire.record = std::move(r);
			return wire;
		}

		if (type == "context.append_message")
		{
			if (!j.contains("message"))
			{
				return absl::InvalidArgumentError("context.append_message missing 'message'");
			}
			auto msg = MessageFromJson(j["message"]);
			if (!msg.ok())
				return msg.status();
			ContextAppendMessageRecord r;
			r.message = std::move(*msg);
			wire.record = std::move(r);
			return wire;
		}

		if (type == "context.append_loop_event")
		{
			if (!j.contains("event"))
			{
				return absl::InvalidArgumentError("context.append_loop_event missing 'event'");
			}
			auto event = LoopEventFromJson(j["event"]);
			if (!event.ok())
				return event.status();
			ContextAppendLoopEventRecord r;
			r.event = std::move(*event);
			wire.record = std::move(r);
			return wire;
		}

		return absl::InvalidArgumentError(fmt::format("unknown record type: {}", type));
	}

} // namespace codeharness::records
