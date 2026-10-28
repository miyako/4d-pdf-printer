#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

struct View : implements<View, IFrameworkView>
{
    void Initialize(CoreApplicationView const& view)
    {
        view.Activated([](auto const&, auto const&) { CoreWindow::GetForCurrentThread().Activate(); });
    }
    void SetWindow(CoreWindow const& window)
    {
        window.Closed([this](auto const&, auto const&) { closed = true; });
    }
    void Load(hstring const&) {}
    void Run()
    {
        auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
        while (!closed) dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
    }
    void Uninitialize() {}
    bool closed = false;
};

struct Source : implements<Source, IFrameworkViewSource>
{
    IFrameworkView CreateView() { return make<View>(); }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    CoreApplication::Run(make<Source>());
}
