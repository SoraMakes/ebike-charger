#include "DiagLog.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    clearTestSerialOutput();
}

void test_history_and_dump_keep_newest_entries_in_oldest_first_order() {
    const uint32_t initialEventSequence = DiagLog::eventSequence();
    for (uint32_t i = 0; i < DiagLog::kCapacity + 2; ++i) {
        testNowMs = i * 1000;
        DiagLog::event("entry %lu", static_cast<unsigned long>(i));
    }

    TEST_ASSERT_EQUAL_UINT8(DiagLog::kCapacity, DiagLog::retainedEntryCount());
    TEST_ASSERT_EQUAL_UINT32(initialEventSequence + DiagLog::kCapacity + 2, DiagLog::eventSequence());

    for (uint8_t i = 0; i < DiagLog::kCapacity; ++i) {
        DiagLog::Entry entry;
        TEST_ASSERT_TRUE(DiagLog::getEntry(i, entry));
        TEST_ASSERT_EQUAL_UINT32((i + 2) * 1000, entry.timestampMs);
        const std::string expected = "entry " + std::to_string(i + 2);
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), entry.text);
    }

    DiagLog::Entry entry;
    TEST_ASSERT_FALSE(DiagLog::getEntry(DiagLog::kCapacity, entry));

    clearTestSerialOutput();
    DiagLog::dump();
    const std::string &output = getTestSerialOutput();

    TEST_ASSERT_TRUE(output.find("entry 1\n") == std::string::npos);

    size_t previousEntryPosition = 0;
    for (uint32_t i = 2; i < DiagLog::kCapacity + 2; ++i) {
        const std::string entry = std::to_string(i) + ".0s | entry " +
            std::to_string(i) + "\n";
        const size_t entryPosition = output.find(entry, previousEntryPosition);
        TEST_ASSERT_TRUE(entryPosition != std::string::npos);
        previousEntryPosition = entryPosition + entry.length();
    }

    uint32_t lineCount = 0;
    for (char character : output) {
        if (character == '\n') {
            ++lineCount;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(DiagLog::kCapacity, lineCount);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_history_and_dump_keep_newest_entries_in_oldest_first_order);
    return UNITY_END();
}
