#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <iostream>

using namespace winrt;
using namespace Windows::Media::Control;

int main()
{
    init_apartment();
    try
    {
        auto managerAsync = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        auto manager = managerAsync.get();
        if (manager)
        {
            auto session = manager.GetCurrentSession();
            std::cout << "Manager created successfully. Has session: " << (session != nullptr) << std::endl;
        }
    }
    catch (const hresult_error& ex)
    {
        std::wcout << L"WinRT Error: " << ex.message().c_str() << std::endl;
    }
    uninit_apartment();
    return 0;
}

