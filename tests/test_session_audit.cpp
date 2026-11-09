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
            auto current = manager.GetCurrentSession();
            if (current)
            {
                std::wcout << L"GetCurrentSession(): " << current.SourceAppUserModelId().c_str() << std::endl;
            }
            else
            {
                std::cout << "GetCurrentSession(): NULL" << std::endl;
            }

            auto sessions = manager.GetSessions();
            std::cout << "GetSessions() count: " << sessions.Size() << std::endl;
            for (uint32_t i = 0; i < sessions.Size(); ++i)
            {
                auto s = sessions.GetAt(i);
                std::wcout << L"[" << i << L"] AppId: " << s.SourceAppUserModelId().c_str() << std::endl;
                auto info = s.GetPlaybackInfo();
                if (info)
                {
                    std::cout << "    Status: " << static_cast<int>(info.PlaybackStatus()) << " (3=Playing, 4=Paused, 5=Stopped)\n";
                    auto controls = info.Controls();
                    if (controls)
                    {
                        std::cout << "    PlayEnabled: " << controls.IsPlayEnabled()
                                  << " PauseEnabled: " << controls.IsPauseEnabled()
                                  << " SeekEnabled: " << controls.IsPlaybackPositionEnabled() << "\n";
                    }
                }
                auto timeline = s.GetTimelineProperties();
                if (timeline)
                {
                    std::cout << "    Position (sec): " << (timeline.Position().count() / 10000000.0)
                              << " Duration (sec): " << (timeline.EndTime().count() / 10000000.0) << "\n";
                }
                auto props = s.TryGetMediaPropertiesAsync().get();
                if (props)
                {
                    std::wcout << L"    Title: " << props.Title().c_str()
                               << L" | Artist: " << props.Artist().c_str() << std::endl;
                }
            }
        }
    }
    catch (const hresult_error& ex)
    {
        std::wcout << L"WinRT error: " << ex.message().c_str() << std::endl;
    }
    return 0;
}

