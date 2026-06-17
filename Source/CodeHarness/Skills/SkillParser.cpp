#include "Skills/SkillParser.h"

#include <algorithm>
#include <filesystem>

#include <yaml-cpp/yaml.h>

#include "absl/status/status.h"

namespace codeharness::skills
{

	namespace
	{
		std::string TrimString(std::string_view sv)
		{
			auto start = sv.find_first_not_of(" \t\r\n");
			if (start == std::string_view::npos)
				return {};
			auto end = sv.find_last_not_of(" \t\r\n");
			return std::string(sv.substr(start, end - start + 1));
		}
	} // namespace

	absl::StatusOr<SkillDefinition> SkillParser::Parse(std::string_view content, const std::string& filePath, SkillSource source)
	{
		std::string_view yaml;
		std::string_view body = content;

		SkillMetadata metadata;
		if (FindFrontmatterBounds(content, yaml, body))
		{
			auto statusOrMetadata = ParseFrontmatter(yaml);
			if (!statusOrMetadata.ok())
				return statusOrMetadata.status();
			metadata = std::move(*statusOrMetadata);
		}
		else
		{
			metadata.name = ExtractName(filePath);
		}

		SkillDefinition def;
		def.name = metadata.name.empty() ? ExtractName(filePath) : metadata.name;
		def.description = metadata.description;
		def.path = filePath;
		def.dir = ExtractDir(filePath);
		def.content = TrimString(body);
		def.metadata = std::move(metadata);
		def.source = source;

		return def;
	}

	absl::StatusOr<SkillMetadata> SkillParser::ParseFrontmatter(std::string_view yaml)
	{
		try
		{
			std::string cleanedYaml(yaml);
			cleanedYaml.erase(std::remove(cleanedYaml.begin(), cleanedYaml.end(), '\r'), cleanedYaml.end());
			YAML::Node node = YAML::Load(cleanedYaml);

			SkillMetadata meta;

			if (node["name"])
				meta.name = node["name"].as<std::string>();

			if (node["description"])
				meta.description = node["description"].as<std::string>();

			if (node["type"])
			{
				auto typeStr = node["type"].as<std::string>();
				auto type = ParseSkillType(typeStr);
				if (type)
					meta.type = *type;
			}

			if (node["whenToUse"])
				meta.whenToUse = node["whenToUse"].as<std::string>();

			if (node["disableModelInvocation"])
				meta.disableModelInvocation = node["disableModelInvocation"].as<bool>();

			if (node["arguments"])
			{
				for (const auto& arg : node["arguments"])
					meta.arguments.push_back(arg.as<std::string>());
			}

			if (node["model"])
				meta.model = node["model"].as<std::string>();

			return meta;
		}
		catch (const YAML::Exception& e)
		{
			return absl::InvalidArgumentError(std::string("YAML parse error: ") + e.what());
		}
	}

	std::string_view SkillParser::ParseBody(std::string_view content)
	{
		std::string_view yaml;
		std::string_view body;
		if (FindFrontmatterBounds(content, yaml, body))
			return body;
		return content;
	}

	bool SkillParser::FindFrontmatterBounds(std::string_view content, std::string_view& yaml, std::string_view& body)
	{
		if (content.size() < 4)
			return false;

		if (content.substr(0, 4) != "---\n" && content.substr(0, 5) != "---\r\n")
			return false;

		auto yamlStart = (content[3] == '\n') ? 4 : 5;

		auto yamlEnd = content.find("\n---", yamlStart);
		if (yamlEnd == std::string_view::npos)
			return false;

		auto bodyStart = yamlEnd + 4;
		while (bodyStart < content.size() && (content[bodyStart] == '\n' || content[bodyStart] == '\r'))
			++bodyStart;

		yaml = content.substr(yamlStart, yamlEnd - yamlStart);
		body = content.substr(bodyStart);

		return true;
	}

	std::string SkillParser::ExtractDir(const std::string& filePath)
	{
		std::filesystem::path p(filePath);
		return p.parent_path().string();
	}

	std::string SkillParser::ExtractName(const std::string& filePath)
	{
		std::filesystem::path p(filePath);
		auto stem = p.stem().string();
		if (stem == "SKILL")
		{
			return p.parent_path().filename().string();
		}
		return stem;
	}

} // namespace codeharness::skills
