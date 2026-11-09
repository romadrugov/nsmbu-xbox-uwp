// Isolated UWP lifecycle probe. No Direct3D, file IO, or Nintendo code.
// A successful test is a blank (black) screen that stays open for at least 20 seconds.
// This distinguishes WinRT activation/packaging failures from GPU initialization.
#include <windows.h>
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

namespace NSMBUXboxSmoke {
public ref class App sealed : IFrameworkView {
public:
    virtual void Initialize(CoreApplicationView^ applicationView) {
        applicationView->Activated +=
            ref new Windows::Foundation::TypedEventHandler<
                CoreApplicationView^,
                Windows::ApplicationModel::Activation::IActivatedEventArgs^>(
                this, &App::OnActivated);
    }

    virtual void SetWindow(CoreWindow^ window) {
        window_ = window;
        window_->Closed +=
            ref new Windows::Foundation::TypedEventHandler<
                CoreWindow^, CoreWindowEventArgs^>(this, &App::OnClosed);
    }

    virtual void Load(Platform::String^ entryPoint) {
        (void)entryPoint;
    }

    virtual void Run() {
        // ProcessOneAndAllPending blocks until the next event, without busy spinning.
        // If this app stays open, the executable's WinRT entrypoint and UWP packaging work.
        while (!closed_) {
            window_->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        }
    }

    virtual void Uninitialize() {}

private:
    void OnActivated(
        CoreApplicationView^ applicationView,
        Windows::ApplicationModel::Activation::IActivatedEventArgs^ args) {
        (void)applicationView;
        (void)args;
        CoreWindow::GetForCurrentThread()->Activate();
    }

    void OnClosed(CoreWindow^ window, CoreWindowEventArgs^ args) {
        (void)window;
        (void)args;
        closed_ = true;
    }

    bool closed_ = false;
    CoreWindow^ window_ = nullptr;
};

ref class SmokeViewSource sealed : IFrameworkViewSource {
public:
    virtual IFrameworkView^ CreateView() {
        return ref new App();
    }
};
} // namespace NSMBUXboxSmoke

[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^ args) {
    (void)args;
    CoreApplication::Run(ref new NSMBUXboxSmoke::SmokeViewSource());
    return 0;
}
