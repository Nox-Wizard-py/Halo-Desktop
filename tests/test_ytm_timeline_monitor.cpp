#include <iostream>
#include <chrono>
#include <thread>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

using namespace winrt;
using namespace winrt::Windows::Media::Control;

int main()
{
    init_apartment();
    auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    auto session = manager.GetCurrentSession();
    if (!session)
    {
        auto sessions = manager.GetSessions();
        if (sessions.Size() > 0) session = sessions.GetAt(0);
    }
    if (!session)
    {
        std::cout << "No media session found!\n";
        return 1;
    }

    std::wcout << L"Monitoring: " << session.SourceAppUserModelId().c_str() << L"\n";

    std::atomic<int> timelineEvents{ 0 };
    auto token = session.TimelinePropertiesChanged([&](auto&&, auto&&) {
        timelineEvents++;
        std::cout << ">>> [EVENT] TimelinePropertiesChanged fired! <<<\n";
    });

    int64_t lastPos = -1;
    int64_t lastTime = -1;

    for (int i = 0; i < 20; ++i)
    {
        auto timeline = session.GetTimelineProperties();
        if (timeline)
        {
            int64_t pos = timeline.Position().count();
            int64_t time = timeline.LastUpdatedTime().time_since_epoch().count();
            bool posChanged = (pos != lastPos);
            bool timeChanged = (time != lastTime);
            lastPos = pos;
            lastTime = time;

            std::cout << "Sample " << i << " (Events=" << timelineEvents.load() << "): Pos=" 
                      << (pos / 10000000.0) << "s | PosChanged=" << posChanged 
                      << " | TimeChanged=" << timeChanged << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    session.TimelinePropertiesChanged(token);
    return 0;
}

