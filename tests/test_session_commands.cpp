#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <iostream>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Media::Control;

int main()
{
    init_apartment();
    try
    {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (manager)
        {
            auto session = manager.GetCurrentSession();
            if (!session)
            {
                auto all = manager.GetSessions();
                std::cout << "Total sessions in GetSessions(): " << all.Size() << std::endl;
                for (const auto& s : all)
                {
                    std::wcout << L"  Available session: " << s.SourceAppUserModelId().c_str() << std::endl;
                    session = s;
                    break;
                }
            }
            if (session)
            {
                std::wcout << L"Active Session: " << session.SourceAppUserModelId().c_str() << std::endl;
                auto info = session.GetPlaybackInfo();
                if (info)
                {
                    std::cout << "Playback Status: " << static_cast<int>(info.PlaybackStatus()) << std::endl;
                    auto controls = info.Controls();
                    if (controls)
                    {
                        std::cout << "Controls:\n";
                        std::cout << "  Play: " << controls.IsPlayEnabled() << "\n";
                        std::cout << "  Pause: " << controls.IsPauseEnabled() << "\n";
                        std::cout << "  Previous: " << controls.IsPreviousEnabled() << "\n";
                        std::cout << "  Next: " << controls.IsNextEnabled() << "\n";
                        std::cout << "  Seek: " << controls.IsPlaybackPositionEnabled() << "\n";
                    }
                }

                auto timeline = session.GetTimelineProperties();
                if (timeline)
                {
                    auto pos = timeline.Position().count(); // 100ns units
                    auto end = timeline.EndTime().count();
                    std::cout << "Timeline:\n";
                    std::cout << "  Position (seconds): " << (pos / 10000000.0) << "\n";
                    std::cout << "  EndTime (seconds): " << (end / 10000000.0) << "\n";
                }
            }
            else
            {
                std::cout << "No active session returned by GetCurrentSession()." << std::endl;
            }
        }
    }
    catch (const hresult_error& ex)
    {
        std::wcout << L"Error: " << ex.message().c_str() << std::endl;
    }
    return 0;
}
