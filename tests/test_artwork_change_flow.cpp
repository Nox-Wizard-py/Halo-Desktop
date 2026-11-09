#include <iostream>
#include <chrono>
#include <thread>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

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

    std::wcout << L"Active Session: " << session.SourceAppUserModelId().c_str() << L"\n";

    auto props = session.TryGetMediaPropertiesAsync().get();
    if (props)
    {
        std::wcout << L"Title: " << props.Title().c_str() << L"\n";
        std::wcout << L"Artist: " << props.Artist().c_str() << L"\n";
        std::wcout << L"Album: " << props.AlbumTitle().c_str() << L"\n";

        auto thumb = props.Thumbnail();
        if (thumb)
        {
            try
            {
                auto stream = thumb.OpenReadAsync().get();
                std::cout << "Thumbnail Stream Size: " << stream.Size() << " bytes\n";
                if (stream.Size() > 0)
                {
                    Buffer buf(std::min<uint32_t>(static_cast<uint32_t>(stream.Size()), 64));
                    stream.ReadAsync(buf, buf.Capacity(), InputStreamOptions::None).get();
                    std::cout << "First bytes: ";
                    for (uint32_t i = 0; i < std::min<uint32_t>(buf.Length(), 16); ++i)
                    {
                        printf("%02X ", buf.data()[i]);
                    }
                    printf("\n");
                }
            }
            catch (const hresult_error& ex)
            {
                std::wcout << L"Failed to read thumbnail: " << ex.message().c_str() << L"\n";
            }
        }
        else
        {
            std::cout << "Thumbnail: NULL\n";
        }
    }

    return 0;
}

