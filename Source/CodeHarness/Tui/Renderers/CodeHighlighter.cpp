#include "Tui/Renderers/CodeHighlighter.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <re2/re2.h>

namespace codeharness::tui
{

	namespace
	{

		using ftxui::Color;
		using ftxui::Element;
		using ftxui::Elements;
		using ftxui::text;

		enum class Tok
		{
			Plain,
			Keyword,
			Type,
			String,
			Comment,
			Number,
			Punct,
		};

		Color TokColor(Tok t)
		{
			switch (t)
			{
			case Tok::Keyword:
				return Color::Magenta;
			case Tok::Type:
				return Color::Yellow;
			case Tok::String:
				return Color::Green;
			case Tok::Comment:
				return Color::GrayLight;
			case Tok::Number:
				return Color::Cyan;
			case Tok::Punct:
				return Color::GrayLight;
			case Tok::Plain:
			default:
				return Color::Default;
			}
		}

		enum class Family
		{
			None,
			CLike,
			Bash,
			Json,
			Python,
		};

		Family ResolveFamily(const std::string& lang)
		{
			if (lang == "cpp" || lang == "c++" || lang == "cc" || lang == "cxx" ||
				lang == "h" || lang == "hpp" || lang == "c")
				return Family::CLike;
			if (lang == "js" || lang == "javascript" || lang == "jsx" ||
				lang == "ts" || lang == "typescript" || lang == "tsx" ||
				lang == "java" || lang == "go" || lang == "golang" ||
				lang == "rs" || lang == "rust" || lang == "php" ||
				lang == "c#" || lang == "cs" || lang == "kt" || lang == "kotlin" ||
				lang == "swift" || lang == "scala" || lang == "dart")
				return Family::CLike;
			if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh")
				return Family::Bash;
			if (lang == "json")
				return Family::Json;
			if (lang == "py" || lang == "python")
				return Family::Python;
			return Family::None;
		}

		const std::unordered_set<std::string>& KeywordTable(Family f)
		{
			static const std::unordered_set<std::string> cLike = {
				"alignas",
				"alignof",
				"and",
				"auto",
				"bool",
				"break",
				"case",
				"catch",
				"char",
				"class",
				"const",
				"constexpr",
				"continue",
				"decltype",
				"default",
				"delete",
				"do",
				"double",
				"else",
				"enum",
				"explicit",
				"export",
				"extern",
				"false",
				"final",
				"float",
				"for",
				"friend",
				"goto",
				"if",
				"inline",
				"int",
				"long",
				"module",
				"mutable",
				"namespace",
				"new",
				"noexcept",
				"nullptr",
				"operator",
				"override",
				"private",
				"protected",
				"public",
				"register",
				"return",
				"short",
				"signed",
				"sizeof",
				"static",
				"static_cast",
				"struct",
				"switch",
				"template",
				"this",
				"throw",
				"true",
				"try",
				"typedef",
				"typename",
				"union",
				"unsigned",
				"using",
				"virtual",
				"void",
				"volatile",
				"while",
				"await",
				"async",
				"function",
				"let",
				"var",
				"yield",
				"typeof",
				"instanceof",
				"in",
				"of",
				"import",
				"from",
				"as",
				"interface",
				"type",
				"extends",
				"implements",
				"package",
				"abstract",
				"synchronized",
				"defer",
				"go",
				"chan",
				"select",
				"map",
				"range",
				"func",
				"fallthrough",
				"crate",
				"fn",
				"impl",
				"match",
				"mut",
				"pub",
				"ref",
				"self",
				"Self",
				"trait",
				"where",
				"unsafe",
				"use",
				"move",
				"box",
				"dyn",
			};
			static const std::unordered_set<std::string> bash = {
				"if",
				"then",
				"else",
				"elif",
				"fi",
				"for",
				"do",
				"done",
				"while",
				"until",
				"case",
				"esac",
				"in",
				"function",
				"return",
				"local",
				"export",
				"unset",
				"readonly",
				"declare",
				"typeset",
				"shift",
				"exit",
				"trap",
				"set",
				"source",
				"echo",
				"printf",
				"read",
			};
			static const std::unordered_set<std::string> py = {
				"and",
				"as",
				"assert",
				"async",
				"await",
				"break",
				"class",
				"continue",
				"def",
				"del",
				"elif",
				"else",
				"except",
				"False",
				"finally",
				"for",
				"from",
				"global",
				"if",
				"import",
				"in",
				"is",
				"lambda",
				"None",
				"nonlocal",
				"not",
				"or",
				"pass",
				"raise",
				"return",
				"True",
				"try",
				"while",
				"with",
				"yield",
				"self",
				"cls",
			};
			static const std::unordered_set<std::string> json = {
				"true",
				"false",
				"null",
			};
			static const std::unordered_set<std::string> none;
			switch (f)
			{
			case Family::CLike:
				return cLike;
			case Family::Bash:
				return bash;
			case Family::Python:
				return py;
			case Family::Json:
				return json;
			case Family::None:
			default:
				return none;
			}
		}

		const std::unordered_set<std::string>& TypeTable(Family f)
		{
			static const std::unordered_set<std::string> cLikeTypes = {
				"int8_t",
				"int16_t",
				"int32_t",
				"int64_t",
				"uint8_t",
				"uint16_t",
				"uint32_t",
				"uint64_t",
				"size_t",
				"ssize_t",
				"ptrdiff_t",
				"intptr_t",
				"uintptr_t",
				"std::string",
				"std::vector",
				"std::map",
				"std::set",
				"std::pair",
				"std::unique_ptr",
				"std::shared_ptr",
				"std::optional",
				"string",
				"vector",
				"map",
				"set",
				"pair",
				"variant",
				"optional",
			};
			static const std::unordered_set<std::string> empty;
			return f == Family::CLike ? cLikeTypes : empty;
		}

		// Build a single anchored regex that splits one line into ordered tokens.
		// Named groups make the per-match classification trivial.
		//
		//   comment   — //... or #... or /* ... */ (within a line)
		//   string    — "..." or '...' with backslash escapes
		//   number    — integer / float / hex / binary
		//   ident     — [A-Za-z_$][A-Za-z0-9_$]*  (then classified as keyword/type/plain)
		//   punct     — runs of operator/punctuation characters
		//   space     — runs of whitespace
		//   other     — any single char (catch-all)
		std::string BuildTokenRegex(Family family)
		{
			// Comments differ by family.
			std::string comment;
			switch (family)
			{
			case Family::Bash:
			case Family::Python:
			case Family::Json:
				comment = R"(#.*$)";
				break;
			case Family::CLike:
				// both // line comment and inline /* ... */
				comment = R"(//[^\n]*|/\*[^*]*\*+(?:[^/*][^*]*\*+)*/)";
				break;
			case Family::None:
			default:
				comment = R"(#.*$)";
				break;
			}

			// Order matters: comment and string first so punctuation inside them
			// doesn't get tokenized separately.
			return std::string("(?P<comment>") + comment + ")" +
				   R"(|(?P<string>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'))" +
				   R"(|(?P<number>\b0[xX][0-9a-fA-F]+\b|\b0[bB][01]+\b|\b\d+\.?\d*(?:[eE][+-]?\d+)?[fFuUlL]*\b|\.\d+(?:[eE][+-]?\d+)?\b))" +
				   R"(|(?P<ident>[A-Za-z_$][A-Za-z0-9_$]*))" +
				   R"(|(?P<punct>[{}\(\)\[\];,<>=+\-*/%&|^~!?:.]+))" +
				   R"(|(?P<space>[ \t\r]+))" +
				   R"(|(?P<other>.))";
		}

		// Classify an identifier as keyword/type/plain using the family's tables.
		Tok ClassifyIdent(const std::string& word, Family family)
		{
			const auto& kw = KeywordTable(family);
			const auto& types = TypeTable(family);
			if (kw.find(word) != kw.end())
				return Tok::Keyword;
			if (types.find(word) != types.end())
				return Tok::Type;
			return Tok::Plain;
		}

		Element RenderLine(std::string_view line, Family family, const RE2& re)
		{
			if (!re.ok())
			{
				return text(std::string(line));
			}

			Elements spans;
			std::string scratch;
			Tok current = Tok::Plain;
			std::string buffer;
			auto flush = [&] {
				if (!buffer.empty())
				{
					spans.push_back(text(buffer) | ftxui::color(TokColor(current)));
					buffer.clear();
				}
			};

			const char* base = line.data();
			size_t size = line.size();
			size_t pos = 0;
			while (pos < size)
			{
				re2::StringPiece sub;
				re2::StringPiece input(base + pos, size - pos);
				bool matched = re.Match(input, 0, input.size(), RE2::UNANCHORED, &sub, 1);
				if (!matched || sub.empty())
				{
					// Avoid infinite loop on a zero-width match — advance one char.
					buffer.push_back(base[pos]);
					++pos;
					continue;
				}

				std::string_view tok(sub.data(), sub.size());
				Tok kind = Tok::Plain;
				if (!tok.empty())
				{
					char c0 = tok[0];
					if (c0 == '#' || (tok.size() >= 2 && tok[0] == '/' && (tok[1] == '/' || tok[1] == '*')))
						kind = Tok::Comment;
					else if (c0 == '"' || c0 == '\'')
						kind = Tok::String;
					else if ((c0 >= '0' && c0 <= '9') || (c0 == '.' && tok.size() > 1 && tok[1] >= '0' && tok[1] <= '9'))
						kind = Tok::Number;
					else if ((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_' || c0 == '$')
					{
						scratch.assign(tok.data(), tok.size());
						kind = ClassifyIdent(scratch, family);
					}
					else if (std::string_view("{}()[];,<>=+-*/%&|^~!?:.").find(c0) != std::string_view::npos)
						kind = Tok::Punct;
					else
						kind = Tok::Plain;
				}

				if (kind != current)
				{
					flush();
					current = kind;
				}
				buffer.append(tok.data(), tok.size());
				pos += tok.size();
			}
			flush();

			if (spans.empty())
				return text(" ");
			if (spans.size() == 1)
				return std::move(spans[0]);
			return ftxui::hbox(std::move(spans));
		}

	} // namespace

	Element CodeHighlighter::Highlight(const std::string& code, const std::string& lang)
	{
		Family family = ResolveFamily(lang);
		if (family == Family::None)
		{
			Elements lines;
			size_t start = 0;
			while (true)
			{
				auto pos = code.find('\n', start);
				if (pos == std::string::npos)
				{
					lines.push_back(text(code.substr(start)));
					break;
				}
				lines.push_back(text(code.substr(start, pos - start)));
				start = pos + 1;
			}
			if (lines.empty())
				return text("");
			if (lines.size() == 1)
				return std::move(lines[0]);
			return ftxui::vbox(std::move(lines));
		}

		// Build the tokenizer regex once per call and reuse it across lines.
		RE2 lineRe(BuildTokenRegex(family));

		Elements lines;
		size_t start = 0;
		while (true)
		{
			auto pos = code.find('\n', start);
			std::string_view slice;
			if (pos == std::string::npos)
			{
				slice = std::string_view(code).substr(start);
			}
			else
			{
				slice = std::string_view(code).substr(start, pos - start);
			}
			lines.push_back(RenderLine(slice, family, lineRe));
			if (pos == std::string::npos)
				break;
			start = pos + 1;
		}
		if (lines.empty())
			return text("");
		if (lines.size() == 1)
			return std::move(lines[0]);
		return ftxui::vbox(std::move(lines));
	}

} // namespace codeharness::tui
