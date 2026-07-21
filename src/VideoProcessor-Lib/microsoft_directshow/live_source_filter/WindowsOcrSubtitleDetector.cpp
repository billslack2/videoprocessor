#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include "WindowsOcrSubtitleDetector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cwctype>
#include <string>
#include <vector>

#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "windowsapp.lib")

namespace
{
	struct __declspec(uuid("905A0FEF-BC53-11DF-8C49-001E4FC686DA"))
		IBufferByteAccess : IUnknown
	{
		virtual HRESULT __stdcall Buffer(uint8_t** value) = 0;
	};

	struct OcrLineBox
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;
		int characters = 0;
		int textCharacters = 0;
		int digitCharacters = 0;
		int symbolCharacters = 0;
		int wordCount = 0;
		int maximumWordGap = 0;
		int lineCount = 1;
		bool containsMusicNote = false;
		bool centered = false;
		std::wstring text;
		std::vector<WindowsOcrWordBox> words;
	};

	bool IsMusicNote(wchar_t character)
	{
		return character == L'\u2669' || character == L'\u266A' ||
			character == L'\u266B' || character == L'\u266C';
	}

	bool IsSubtitlePunctuation(wchar_t character)
	{
		switch (character)
		{
		case L' ':
		case L'\t':
		case L'.':
		case L',':
		case L'!':
		case L'?':
		case L':':
		case L';':
		case L'\'':
		case L'"':
		case L'-':
		case L'_':
		case L'(':
		case L')':
		case L'[':
		case L']':
		case L'{':
		case L'}':
		case L'/':
		case L'\\':
		case L'\u2026':
		case L'\u2013':
		case L'\u2014':
			return true;
		default:
			return false;
		}
	}

	uint64_t HashSubtitleText(const std::wstring& text)
	{
		// Stable, allocation-free FNV-1a over normalized caption text. Collapse
		// whitespace and case so harmless OCR formatting differences do not turn
		// one displayed caption into a sequence of different identities.
		uint64_t hash = 1469598103934665603ULL;
		bool priorWasSpace = true;
		for (wchar_t character : text)
		{
			if (std::iswspace(character))
			{
				if (priorWasSpace)
					continue;
				character = L' ';
				priorWasSpace = true;
			}
			else
			{
				character = static_cast<wchar_t>(std::towlower(character));
				priorWasSpace = false;
			}
			hash ^= static_cast<uint16_t>(character);
			hash *= 1099511628211ULL;
		}
		return hash;
	}
}

WindowsOcrSubtitleResult DetectWindowsOcrSubtitle(
	const uint16_t* luma,
	int width,
	int height,
	int pictureTop,
	int pictureBottom)
{
	WindowsOcrSubtitleResult result;
	if (!luma || width < 80 || height < 60 ||
		pictureTop < 0 || pictureBottom <= pictureTop ||
		pictureBottom > height)
		return result;

	try
	{
		static thread_local bool apartmentInitialized = false;
		static thread_local bool engineAttempted = false;
		static thread_local winrt::Windows::Media::Ocr::OcrEngine engine{ nullptr };
		if (!apartmentInitialized)
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			apartmentInitialized = true;
		}
		if (!engineAttempted)
		{
			engine = winrt::Windows::Media::Ocr::OcrEngine::
				TryCreateFromUserProfileLanguages();
			engineAttempted = true;
		}
		result.available = static_cast<bool>(engine);
		if (!engine)
			return result;

		const int boundarySearchHeight = std::max(16, height / 5);
		const int topSearchBottom =
			std::min(height, pictureTop + boundarySearchHeight);
		const int bottomSearchTop =
			std::max(0, pictureBottom - boundarySearchHeight);
		constexpr int separatorRows = 8;
		const bool packBoundaryBands =
			topSearchBottom + separatorRows < bottomSearchTop;
		const int packedBottomOffset =
			packBoundaryBands ? topSearchBottom + separatorRows : 0;
		const int ocrHeight = packBoundaryBands ?
			topSearchBottom + separatorRows + (height - bottomSearchTop) :
			height;
		const uint32_t pixelCount =
			static_cast<uint32_t>(width * ocrHeight);
		winrt::Windows::Storage::Streams::Buffer buffer(pixelCount);
		buffer.Length(pixelCount);
		uint8_t* pixels = nullptr;
		winrt::check_hresult(
			buffer.as<IBufferByteAccess>()->Buffer(&pixels));
		for (int y = 0; y < ocrHeight; ++y)
		{
			uint8_t* destination =
				pixels + static_cast<size_t>(y) * width;
			int sourceY = y;
			if (packBoundaryBands)
			{
				if (y >= topSearchBottom && y < packedBottomOffset)
				{
					std::fill_n(destination, width, static_cast<uint8_t>(0));
					continue;
				}
				if (y >= packedBottomOffset)
					sourceY = bottomSearchTop + (y - packedBottomOffset);
			}
			else if (y >= topSearchBottom && y < bottomSearchTop)
			{
				std::fill_n(destination, width, static_cast<uint8_t>(0));
				continue;
			}
			const uint16_t* source =
				luma + static_cast<size_t>(sourceY) * width;
			for (int x = 0; x < width; ++x)
				destination[x] = static_cast<uint8_t>(
					std::min<uint16_t>(255, source[x] >> 2));
		}

		const auto bitmap =
			winrt::Windows::Graphics::Imaging::SoftwareBitmap::
			CreateCopyFromBuffer(
				buffer,
				winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Gray8,
				width,
				ocrHeight,
				winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
		const auto recognized = engine.RecognizeAsync(bitmap).get();

		std::vector<OcrLineBox> lines;
		for (const auto& line : recognized.Lines())
		{
			OcrLineBox box{ width, height, 0, 0, 0 };
			int words = 0;
			for (const auto& word : line.Words())
			{
				const auto bounds = word.BoundingRect();
				const int packedTop =
					std::max(0, static_cast<int>(std::floor(bounds.Y)));
				const int packedBottom =
					std::min(ocrHeight,
						static_cast<int>(std::ceil(bounds.Y + bounds.Height)));
				int mappedTop = packedTop;
				int mappedBottom = packedBottom;
				if (packBoundaryBands)
				{
					if (packedBottom <= topSearchBottom)
					{
						// Top band retains its original coordinates.
					}
					else if (packedTop >= packedBottomOffset)
					{
						mappedTop =
							bottomSearchTop + (packedTop - packedBottomOffset);
						mappedBottom =
							bottomSearchTop + (packedBottom - packedBottomOffset);
					}
					else
					{
						// Ignore an OCR word touching the artificial separator.
						continue;
					}
				}
				const WindowsOcrWordBox wordBox{
					std::max(0, static_cast<int>(std::floor(bounds.X))),
					mappedTop,
					std::min(width,
						static_cast<int>(std::ceil(bounds.X + bounds.Width))),
					mappedBottom
				};
				if (wordBox.right <= wordBox.left ||
					wordBox.bottom <= wordBox.top)
					continue;
				box.left = std::min(box.left, wordBox.left);
				box.top = std::min(box.top, wordBox.top);
				box.right = std::max(box.right, wordBox.right);
				box.bottom = std::max(box.bottom, wordBox.bottom);
				box.words.push_back(wordBox);
				++words;
			}
			box.characters = static_cast<int>(line.Text().size());
			const std::wstring lineText = line.Text().c_str();
			box.text = lineText;
			for (const wchar_t character : lineText)
			{
				if (IsMusicNote(character))
				box.containsMusicNote = true;
				else if (std::iswdigit(character))
				{
					++box.textCharacters;
					++box.digitCharacters;
				}
				else if (std::iswalpha(character) ||
					(character >= 0x80 && !IsSubtitlePunctuation(character)))
					++box.textCharacters;
				else if (!IsSubtitlePunctuation(character))
					++box.symbolCharacters;
			}
			box.wordCount = words;
			std::sort(box.words.begin(), box.words.end(),
				[](const WindowsOcrWordBox& a,
					const WindowsOcrWordBox& b) {
					return a.left < b.left;
				});
			for (size_t wordIndex = 1; wordIndex < box.words.size(); ++wordIndex)
				box.maximumWordGap = std::max(box.maximumWordGap,
					box.words[wordIndex].left -
						box.words[wordIndex - 1].right);
			const int lineWidth = box.right - box.left;
			const int lineHeight = box.bottom - box.top;
			const int centerX = box.left + lineWidth / 2;
			box.centered =
				centerX >= width * 3 / 10 && centerX <= width * 7 / 10 &&
				box.right >= width * 2 / 5 &&
				box.left <= width * 3 / 5;
			const int meaningfulCharacters =
				box.textCharacters + (box.containsMusicNote ? 1 : 0);
			const bool subtitleCharacters =
				meaningfulCharacters >= (box.containsMusicNote ? 1 : 3) &&
				(box.characters == 0 ||
					box.textCharacters * 100 >= box.characters * 30 ||
					box.containsMusicNote) &&
				box.symbolCharacters * 100 <=
					std::max(1, box.characters) * 45;
			const bool navigationSpacing =
				words >= 4 &&
				box.maximumWordGap >
					std::max(width / 10, lineHeight * 6);
			if (words > 0 && subtitleCharacters && !navigationSpacing &&
				// Keep the BASIC path's geometry guard consistent with the
				// DirectML path.  A caption can be wide, but near-picture-width
				// text is normally a menu, title card, or false OCR region.
				lineWidth >= width / 50 && lineWidth <= width * 18 / 25 &&
				lineHeight >= 3 && lineHeight <= height / 9)
				lines.push_back(box);
		}

		std::sort(lines.begin(), lines.end(),
			[](const OcrLineBox& a, const OcrLineBox& b) {
				return a.top < b.top;
			});
		result.textObserved = !lines.empty();
		for (size_t anchorIndex = 0; anchorIndex < lines.size(); ++anchorIndex)
		{
			const OcrLineBox& anchor = lines[anchorIndex];
			if (!anchor.centered)
				continue;
			const int boundaryTolerance = std::max(2, height / 240);
			const bool topAnchor =
				pictureTop > 0 &&
				anchor.top < pictureTop + boundaryTolerance;
			const bool bottomAnchor =
				anchor.bottom > pictureBottom - boundaryTolerance;
			if (!topAnchor && !bottomAnchor)
				continue;

			OcrLineBox block = anchor;
			int blockScore =
				anchor.characters * 24 + (anchor.right - anchor.left);
			int includedLines = 1;
			if (bottomAnchor)
			{
				for (size_t priorIndex = anchorIndex;
					priorIndex-- > 0 && includedLines < 3;)
				{
					const OcrLineBox& prior = lines[priorIndex];
					const int gap = block.top - prior.bottom;
					const int blockCenter =
						block.left + (block.right - block.left) / 2;
					const int priorCenter =
						prior.left + (prior.right - prior.left) / 2;
					if (gap < 0 || gap > height / 18 ||
						std::abs(blockCenter - priorCenter) > width / 5)
						break;
					block.left = std::min(block.left, prior.left);
					block.top = prior.top;
					block.right = std::max(block.right, prior.right);
					blockScore += prior.characters * 24 +
						(prior.right - prior.left);
					block.words.insert(block.words.end(),
						prior.words.begin(), prior.words.end());
					block.characters += prior.characters;
					block.textCharacters += prior.textCharacters;
					block.digitCharacters += prior.digitCharacters;
					block.symbolCharacters += prior.symbolCharacters;
					block.wordCount += prior.wordCount;
					block.maximumWordGap = std::max(
						block.maximumWordGap, prior.maximumWordGap);
					block.containsMusicNote |= prior.containsMusicNote;
					block.text = prior.text + L"\n" + block.text;
					++block.lineCount;
					++includedLines;
				}
			}
			else
			{
				for (size_t nextIndex = anchorIndex + 1;
					nextIndex < lines.size() && includedLines < 3;
					++nextIndex)
				{
					const OcrLineBox& next = lines[nextIndex];
					const int gap = next.top - block.bottom;
					const int blockCenter =
						block.left + (block.right - block.left) / 2;
					const int nextCenter =
						next.left + (next.right - next.left) / 2;
					if (gap < 0 || gap > height / 18 ||
						std::abs(blockCenter - nextCenter) > width / 5)
						break;
					block.left = std::min(block.left, next.left);
					block.bottom = next.bottom;
					block.right = std::max(block.right, next.right);
					blockScore += next.characters * 24 +
						(next.right - next.left);
					block.words.insert(block.words.end(),
						next.words.begin(), next.words.end());
					block.characters += next.characters;
					block.textCharacters += next.textCharacters;
					block.digitCharacters += next.digitCharacters;
					block.symbolCharacters += next.symbolCharacters;
					block.wordCount += next.wordCount;
					block.maximumWordGap = std::max(
						block.maximumWordGap, next.maximumWordGap);
					block.containsMusicNote |= next.containsMusicNote;
					block.text += L"\n" + next.text;
					++block.lineCount;
					++includedLines;
				}
			}

			const int center =
				block.left + (block.right - block.left) / 2;
			const int blockWidth = block.right - block.left;
			const int blockHeight = block.bottom - block.top;
			int edgeLineCount = 0;
			int edgeWordCount = 0;
			for (const OcrLineBox& edgeLine : lines)
			{
				const bool sameEdge = topAnchor ?
					edgeLine.top < topSearchBottom :
					edgeLine.bottom > bottomSearchTop;
				if (sameEdge)
				{
					++edgeLineCount;
					edgeWordCount += edgeLine.wordCount;
				}
			}

			// Subtitles are a small coherent text block. A dense set of
			// unrelated OCR lines in the same boundary region is much more
			// likely to be a menu, scoreboard, guide, or application OSD.
			const bool clutteredRegion =
				edgeLineCount > block.lineCount + 3 &&
				edgeWordCount > block.wordCount + 8;
			const bool navigationRow =
				block.wordCount >= 6 && blockWidth >= width * 4 / 5 &&
				block.maximumWordGap >
					std::max(width / 14, blockHeight * 2);
			const bool implausibleCaption =
				block.lineCount < 1 || block.lineCount > 3 ||
				block.wordCount < 1 || block.wordCount > 32 ||
				block.characters > 220 ||
				blockWidth > width * 18 / 25 ||
				blockHeight > height / 5 ||
				center < width * 3 / 10 || center > width * 7 / 10 ||
				clutteredRegion || navigationRow;
			if (implausibleCaption)
				continue;

			blockScore -= std::abs(center - width / 2) / 2;
			if (blockScore > result.score)
			{
				result.left = block.left;
				result.top = block.top;
				result.right = block.right;
				result.bottom = block.bottom;
				result.score = blockScore;
				result.lineCount = block.lineCount;
				result.representativeLineHeight =
					std::max(1, blockHeight / block.lineCount);
				result.contentHash = HashSubtitleText(block.text);
				result.atTop = topAnchor && !bottomAnchor;
				result.words = block.words;
			}
		}
		result.detected = result.score > 0;
		return result;
	}
	catch (...)
	{
		return result;
	}
}
