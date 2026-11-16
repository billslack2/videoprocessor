#include "pch.h"
#include "CppUnitTest.h"
#include <DeckLinkShortcutSelection.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(DeckLinkShortcutSelectionTests)
    {
    public:
        TEST_METHOD(IndexedDeckLinkSelectsExactRequestedInputIgnoringCase)
        {
            const std::vector<std::wstring> devices = {
                L"Magewell HDMI", L"blackmagic decklink duo (2)",
                L"DeckLink Duo (1)", L"DeckLink Duo (10)" };
            Assert::AreEqual(2, DeckLinkShortcutIndex(devices, 1));
            Assert::AreEqual(1, DeckLinkShortcutIndex(devices, 2));
            Assert::AreEqual(-1, DeckLinkShortcutIndex(devices, 3));
            Assert::AreEqual(-1, DeckLinkShortcutIndex(devices, 4));
            const std::vector<std::wstring> tenOnly = { L"DeckLink Duo (10)" };
            Assert::AreEqual(-1, DeckLinkShortcutIndex(tenOnly, 1),
                L"Index 1 must not match input 10.");
        }

        TEST_METHOD(UnindexedDeckLinkIsSelectedForAllFourShortcuts)
        {
            const std::vector<std::wstring> devices = {
                L"Magewell", L"DeckLink Mini Recorder 4K", L"DeckLink SDI" };
            for (int shortcut = 1; shortcut <= 4; ++shortcut)
                Assert::AreEqual(1, DeckLinkShortcutIndex(devices, shortcut));
        }

        TEST_METHOD(MissingOrMixedIndexedInputsNeverSelectAnotherDevice)
        {
            Assert::AreEqual(-1, DeckLinkShortcutIndex({}, 1));
            Assert::AreEqual(-1, DeckLinkShortcutIndex({ L"Magewell HDMI" }, 1));
            const std::vector<std::wstring> mixed = {
                L"DeckLink Mini Recorder 4K", L"DeckLink Duo (2)", L"Magewell" };
            Assert::AreEqual(-1, DeckLinkShortcutIndex(mixed, 1),
                L"An indexed DeckLink anywhere suppresses the single-input fallback.");
            Assert::AreEqual(1, DeckLinkShortcutIndex(mixed, 2));
            Assert::AreEqual(-1, DeckLinkShortcutIndex(mixed, 4));
        }
    };
}
