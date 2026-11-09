#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>

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
            if (session)
            {
                std::wcout << L"Source App: " << session.SourceAppUserModelId().c_str() << std::endl;
                auto info = session.GetPlaybackInfo();
                if (info)
                {
                    std::cout << "Playback Status: " << (int)info.PlaybackStatus() << std::endl;
                }
                auto props = session.TryGetMediaPropertiesAsync().get();
                if (props)
                {
                    std::wcout << L"Title: " << props.Title().c_str() << std::endl;
                    std::wcout << L"Artist: " << props.Artist().c_str() << std::endl;
                    std::wcout << L"Album: " << props.AlbumTitle().c_str() << std::endl;
                    std::wcout << L"Has Thumbnail: " << (props.Thumbnail() != nullptr) << std::endl;
                }
            }
        }
    }
    catch (const hresult_error& ex)
    {
        std::wcout << L"WinRT Error: " << ex.message().c_str() << std::endl;
    }
    uninit_apartment();
    return 0;
}

