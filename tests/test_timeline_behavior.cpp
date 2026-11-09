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

    std::wcout << L"Session: " << session.SourceAppUserModelId().c_str() << L"\n";

    for (int i = 0; i < 10; ++i)
    {
        auto timeline = session.GetTimelineProperties();
        if (timeline)
        {
            auto pos = timeline.Position().count() / 10000000.0;
            auto dur = timeline.EndTime().count() / 10000000.0;
            auto lastUpdated = timeline.LastUpdatedTime().time_since_epoch().count();
            std::cout << "Sample " << i << ": Position = " << pos << "s / " << dur 
                      << "s | LastUpdatedTime = " << lastUpdated << "\n";
        }
        else
        {
            std::cout << "Sample " << i << ": No timeline\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}

