#ifndef OPENMW_COMPONENTS_MISC_BINDINGCAPTURE_H
#define OPENMW_COMPONENTS_MISC_BINDINGCAPTURE_H
#include <optional>
#include <utility>
namespace Misc
{
    // The first physical press owns the recording until its matching release. This
    // prevents the key that completes a rebind from also firing its new action.
    class BindingCapture
    {
        bool mActive = false;
        std::optional<std::pair<int, int>> mPressed, mResult;
    public:
        void begin() { mActive = true; mPressed.reset(); mResult.reset(); }
        void cancel() { mActive = false; mPressed.reset(); mResult.reset(); }
        bool active() const { return mActive; }
        bool event(int device, int code, bool pressed)
        {
            if (!mActive) return false;
            const auto binding = std::make_pair(device, code);
            if (pressed && !mPressed) mPressed = binding;
            if (!pressed && mPressed == binding)
            {
                mResult = binding;
                mActive = false;
                mPressed.reset();
            }
            return true;
        }
        std::optional<std::pair<int, int>> take() { return std::exchange(mResult, std::nullopt); }
    };
}
#endif
