#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/PassiveDisplayProtocol.h>
#include <string>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(PassiveDisplayProtocolTests) {
public:
    TEST_METHOD(ParsesExactEventsAndHealthWithoutConvertingUnknownToZero) {
        PassiveDisplayProtocol::Message m;
        Assert::IsTrue(PassiveDisplayProtocol::Parse("HELLO 1 10000000",m));
        Assert::AreEqual(uint64_t(10000000),m.frequency);
        Assert::IsTrue(PassiveDisplayProtocol::Parse("EVENT 123 456 0x1ABC 10001 20001 1 5 0x0",m));
        Assert::AreEqual(uint32_t(123),m.processId);
        Assert::AreEqual(uint64_t(0x1ABC),m.address);
        Assert::IsTrue(PassiveDisplayProtocol::Parse("STATUS 0 4 2 7",m));
        Assert::IsTrue(m.lossKnown); Assert::AreEqual(uint64_t(7),m.overflows);
        for(const auto line : {"STATUS 5 -1 -1 -1","STATUS 0 0 -1 0","STATUS 0 -1 0 0","STATUS 0 0 0 -1"}) {
            Assert::IsTrue(PassiveDisplayProtocol::Parse(line,m)); Assert::IsFalse(m.lossKnown);
        }
        Assert::IsTrue(PassiveDisplayProtocol::Parse("ERROR 5",m));
        Assert::AreEqual(uint32_t(5),m.status);
    }
    TEST_METHOD(RejectsMalformedAndOverflowingMessagesWithoutChangingOutput) {
        for(const auto line : {"", "HELLO 1 0", "HELLO 1 -1", "HELLO 1 9223372036854775808", "HELLO 1 100 extra",
            "HELLO 4294967296 100", "STATUS 0 0 0 18446744073709551616", "STATUS 0 -2 0 0", "STATUS 0 -1x 0 0",
            "STATUS 0 0 0 0 junk", "ERROR -5", "ERROR 5 extra", "EVENT 1 2 123 4 5 1 5 0x0",
            "EVENT 1 2 0x123 4 5 3 5 0x0", "EVENT 1 2 0x123 9223372036854775808 5 1 5 0x0",
            "EVENT 1 2 0x123 4 5 1 5 0x0 trailing", "EVENT 1 2 0x123 4 5 1 5 0x100000000", "OTHER 1 2"}) {
            PassiveDisplayProtocol::Message m; m.status=777;
            Assert::IsFalse(PassiveDisplayProtocol::Parse(line,m)); Assert::AreEqual(uint32_t(777),m.status);
        }
        PassiveDisplayProtocol::Message m;
        Assert::IsFalse(PassiveDisplayProtocol::Parse(nullptr,m));
        Assert::IsFalse(PassiveDisplayProtocol::Parse(std::string(256,'X').c_str(),m));
    }
};
}
