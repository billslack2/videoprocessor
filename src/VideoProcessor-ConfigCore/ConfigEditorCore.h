#pragma once

#include <string>
#include <utility>
#include <vector>

// UI-independent document and persistence primitives for VideoProcessor.cfg.
//
// The document intentionally stores its contents as byte strings.  Structured
// callers edit only the fields they own; comments, unfamiliar settings, manual
// rules, shaders, and the rest of the original file remain untouched.
namespace ConfigEditorCore
{
	struct ConfigDocument
	{
		std::wstring path;
		std::vector<std::string> lines;
		std::string lineEnding = "\r\n";
		bool hasTerminalLineEnding = false;
		// Exact bytes observed by Load(). SaveSafely normally compares these with
		// the current file immediately before writing; the Config editor can
		// explicitly request an overwrite instead of rejecting a stale-file conflict.
		std::string loadedBytes;
		bool existedAtLoad = false;

		bool Load(const std::wstring& input, std::wstring& error);

		static std::string StripComment(const std::string& value);
		bool Find(const std::string& wantedSection,
			const std::string& wantedKey, size_t& lineIndex,
			size_t& valueStart, size_t& valueEnd) const;
		std::string Get(const char* section, const char* key) const;
		bool SetExisting(const char* section, const char* key,
			const std::string& value);
		bool SetKnown(const std::string& wantedSection, const char* key,
			const std::string& value);
		bool RemoveKnown(const std::string& wantedSection, const char* key);
		bool AddSection(const std::string& section);
		bool RemoveSection(const std::string& wantedSection);
		bool FindSectionHeader(const std::string& wantedSection,
			size_t& lineIndex, size_t& nameStart, size_t& nameEnd) const;
		bool RenameSection(const std::string& oldSection,
			const std::string& newSection);
		bool SwapSectionHeaders(const std::string& first,
			const std::string& second);
		bool MoveSectionBefore(const std::string& wantedSection,
			const std::string& beforeSection);
		bool MoveSectionAfter(const std::string& wantedSection,
			const std::string& afterSection);
		std::vector<std::string> SectionNamesWithPrefix(
			const std::string& prefix) const;
		std::vector<std::string> SectionNames() const;
		std::vector<std::pair<std::string, std::string>> SectionSettings(
			const std::string& section) const;
		std::string Serialize() const;
	};

	struct SaveResult
	{
		std::wstring backupPath;
	};

	// Validates the exact serialized bytes using VP's normal configuration
	// parser and schemas. No in-memory document data is modified.
	bool ValidateCandidate(const ConfigDocument& document,
		std::wstring& error);

	// Validates first, writes a sibling temporary file, then atomically replaces
	// the configuration. On failure the original configuration remains in place.
	// When overwriteExternalChanges is true, a changed or newly-created on-disk
	// file is replaced after the candidate passes validation.
	bool SaveSafely(ConfigDocument& document, SaveResult& result,
		std::wstring& error, bool overwriteExternalChanges = false);
}
