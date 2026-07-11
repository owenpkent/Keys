// Unit tests for Keys' pure logic. JUCE UnitTest framework, no extra dependency.
// Run: cmake --build build --target Keys_tests && ctest --test-dir build
#include "NoteMath.h"
#include <juce_core/juce_core.h>

namespace
{
constexpr int cMajor = 0; // "Major" is index 0 in okstudio::scales::all()

struct NoteMathTests : juce::UnitTest
{
    NoteMathTests() : juce::UnitTest("keys::resolveOutputNote") {}

    void runTest() override
    {
        beginTest("no scale-lock: octave transpose, clamped");
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, 0), 60);
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, 1), 72);
        expectEquals(keys::resolveOutputNote(60, false, 0, cMajor, -1), 48);
        expectEquals(keys::resolveOutputNote(61, false, 0, cMajor, 0), 61); // no snap when off
        expectEquals(keys::resolveOutputNote(120, false, 0, cMajor, 3), 127); // clamp high
        expectEquals(keys::resolveOutputNote(5, false, 0, cMajor, -3), 0);     // clamp low

        beginTest("scale-lock snaps first, then transposes");
        expectEquals(keys::resolveOutputNote(61, true, 0, cMajor, 0), 60);  // C# -> C
        expectEquals(keys::resolveOutputNote(61, true, 0, cMajor, 1), 72);  // C# -> C, +1 octave
        expectEquals(keys::resolveOutputNote(66, true, 0, cMajor, 0), 65);  // F# -> F
        expectEquals(keys::resolveOutputNote(64, true, 0, cMajor, 0), 64);  // E already in scale
    }
};

static NoteMathTests noteMathTests;
} // namespace

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* r = runner.getResult(i))
            failures += r->failures;

    return failures == 0 ? 0 : 1;
}
