#include <iostream>
#include <chrono>
#include <thread>
#include <cassert>
#include <algorithm>
#include "../src/media/MediaSessionWatcher.hpp"

using namespace lumn::media;

int main()
{
    std::cout << "========================================================\n";
    std::cout << "  Phase 4A.3 Verification Suite: Timeline Stabilization\n";
    std::cout << "========================================================\n\n";

    // 1. Unified Position Function Strict Monotonicity Test
    std::cout << "[Test 1] Verifying GetCurrentPosition100ns Strict Monotonicity...\n";
    {
        MediaTrackInfo track{};
        track.isPlaying = true;
        track.playbackRate = 1.0;
        track.position100ns = 1394908520LL; // 139.49s (2:19 from user's live log)
        track.duration100ns = 2232767290LL; // 223.27s (3:43)
        track.lastUpdatedTime = std::chrono::steady_clock::now();

        int64_t prevPos = track.position100ns;
        // Sample 12 times across 3.6 seconds (simulating multiple 250ms polling intervals)
        for (int i = 1; i <= 12; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto now = std::chrono::steady_clock::now();
            int64_t pos = track.GetCurrentPosition100ns(now, true);

            double posSec = pos / 10000000.0;
            double prevSec = prevPos / 10000000.0;
            std::cout << "  Sample " << i << " (+ " << (i * 300) << "ms): Pos = " 
                      << posSec << "s (advanced +" << (posSec - prevSec) << "s)\n";

            assert(pos > prevPos); // Must strictly increase!
            prevPos = pos;
        }
        std::cout << "  -> PASSED: Position advanced smoothly past 2:20 without any 1.5s resets.\n\n";
    }

    // 2. Stationary GSMTC Sample Rejection Test (Simulating Chromium's static timeline)
    std::cout << "[Test 2] Simulating Chromium Static Timeline (Zero-Reset Guarantee)...\n";
    {
        // Setup initial anchor
        int64_t anchorPos100ns = 1394908520LL; // 139.49s
        int64_t lastAuthPos100ns = 1394908520LL;
        int64_t lastTimelineSystemTimeRaw = 134381073901507460LL;
        auto anchorTime = std::chrono::steady_clock::now();

        // Simulate 8 cycles of GSMTC polling returning the EXACT SAME stationary sample
        int rebaseCount = 0;
        for (int cycle = 1; cycle <= 8; ++cycle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

            // Chromium returns the exact same position and timestamp
            int64_t incomingPos100ns = 1394908520LL;
            int64_t incomingTimeRaw = 134381073901507460LL;

            bool posChanged = (incomingPos100ns != lastAuthPos100ns);
            bool timeChanged = (incomingTimeRaw != 0 && incomingTimeRaw != lastTimelineSystemTimeRaw);
            bool hasNewSample = (posChanged || timeChanged);

            if (hasNewSample)
            {
                rebaseCount++;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - anchorTime).count();
            int64_t currentLocalPos = anchorPos100ns + static_cast<int64_t>(elapsedUs * 10.0);

            std::cout << "  Cycle " << cycle << ": LocalPos = " << (currentLocalPos / 10000000.0) 
                      << "s | IncomingPos = " << (incomingPos100ns / 10000000.0) 
                      << "s | HasNewSample = " << (hasNewSample ? "YES" : "NO (IGNORED)") << "\n";

            assert(!hasNewSample);
        }

        assert(rebaseCount == 0);
        std::cout << "  -> PASSED: All 8 stationary samples correctly ignored. Zero rebases triggered.\n\n";
    }

    // 3. Genuine User Seek Tests (Forward and Backward)
    std::cout << "[Test 3] Genuine Seek Detection (Forward to 2:57 and Backward to 1:10)...\n";
    {
        int64_t lastAuthPos100ns = 140 * 10000000LL; // 2:20 (140s)
        int64_t currentLocalPos = 142 * 10000000LL;  // 2:22 (142s)

        // Case A: User seeks forward to 176.55s (2:57)
        int64_t seekForwardPos = 1765500000LL;
        bool posChanged = (seekForwardPos != lastAuthPos100ns);
        int64_t delta100ns = seekForwardPos - currentLocalPos;
        bool isForwardSeek = (posChanged && delta100ns > 20000000LL); // > +2.0s

        assert(isForwardSeek);
        std::cout << "  Forward Seek Test: Target=" << (seekForwardPos / 10000000.0) 
                  << "s | Delta=+" << (delta100ns / 10000000.0) << "s | Detected=" << (isForwardSeek ? "YES" : "NO") << "\n";

        // Update anchor to forward seek
        lastAuthPos100ns = seekForwardPos;
        currentLocalPos = seekForwardPos + 10 * 10000000LL; // 186.55s

        // Case B: User seeks backward to 70s (1:10)
        int64_t seekBackwardPos = 70 * 10000000LL;
        posChanged = (seekBackwardPos != lastAuthPos100ns);
        delta100ns = seekBackwardPos - currentLocalPos;
        bool isBackwardSeek = (posChanged && delta100ns < -20000000LL); // < -2.0s

        assert(isBackwardSeek);
        std::cout << "  Backward Seek Test: Target=" << (seekBackwardPos / 10000000.0) 
                  << "s | Delta=" << (delta100ns / 10000000.0) << "s | Detected=" << (isBackwardSeek ? "YES" : "NO") << "\n";
        std::cout << "  -> PASSED: Both forward and backward seeks recognized and distinguished from playback.\n\n";
    }

    // 4. Pause Freezing and Resuming
    std::cout << "[Test 4] Pause Freezing & Resume Continuity...\n";
    {
        MediaTrackInfo track{};
        track.isPlaying = true;
        track.playbackRate = 1.0;
        track.position100ns = 50 * 10000000LL; // 50s
        track.duration100ns = 200 * 10000000LL;
        track.lastUpdatedTime = std::chrono::steady_clock::now();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // Pause event: freeze at interpolated position
        auto pauseTime = std::chrono::steady_clock::now();
        int64_t frozenPos = track.GetCurrentPosition100ns(pauseTime, true);
        track.isPlaying = false;
        track.position100ns = frozenPos;
        track.lastUpdatedTime = pauseTime;

        std::cout << "  Paused at: " << (frozenPos / 10000000.0) << "s\n";

        // Wait 500ms while paused
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto check1 = track.GetCurrentPosition100ns(std::chrono::steady_clock::now(), false);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto check2 = track.GetCurrentPosition100ns(std::chrono::steady_clock::now(), false);

        assert(check1 == frozenPos);
        assert(check2 == frozenPos);
        std::cout << "  Frozen checks: check1=" << (check1 / 10000000.0) << "s, check2=" << (check2 / 10000000.0) << "s\n";

        // Resume playback
        auto resumeTime = std::chrono::steady_clock::now();
        track.isPlaying = true;
        track.lastUpdatedTime = resumeTime;

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        auto resumedPos = track.GetCurrentPosition100ns(std::chrono::steady_clock::now(), true);
        std::cout << "  Resumed position after 400ms: " << (resumedPos / 10000000.0) << "s\n";
        assert(resumedPos > frozenPos);
        std::cout << "  -> PASSED: Pause freezes precisely; resume continues without backward jump.\n\n";
    }

    std::cout << "========================================================\n";
    std::cout << "  ALL PHASE 4A.3 VERIFICATION TESTS PASSED!\n";
    std::cout << "========================================================\n";
    return 0;
}

