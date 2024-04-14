#include "PDA-Injector.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <vector>
#include <thread>
#include <string_view>
#include <shared_mutex>
#include <regex>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#include "fmt/include/fmt/format.h"

#include "joystick.h"
#include "mouse.h"

using winrt::Windows::Gaming::Input::GameControllerSwitchPosition;

extern POINT appMousePos;
extern time_t appLastMouseMove;
extern bool appMouseButtonDown;

namespace joystick {

    constexpr std::array<std::string_view, 9> switch_pos_names = {
        "Center\0",
        "Up\0",
        "UpRight\0",
        "Right\0",
        "DownRight\0",
        "Down\0",
        "DownLeft\0",
        "Left\0",
        "UpLeft\0"
    };

    struct ctrl_status_t {
        int num_buttons = 0;
        std::array<bool, 128> buttons;
        std::vector<GameControllerSwitchPosition> switches;
        std::vector<double> axis;

        uint64_t timestamp = 0;
        uint64_t num_readouts = 0;
        bool use_for_mouse = false;

        ctrl_status_t() { std::fill(buttons.begin(), buttons.end(), false); }

        ctrl_status_t(int in_num_buttons, int num_switches, int num_axis) : num_buttons(in_num_buttons),
            switches(num_switches), axis(num_axis) { 
            std::fill(buttons.begin(), buttons.end(), false);
        }

        ctrl_status_t(ctrl_status_t&& st) noexcept : num_buttons(st.num_buttons),
            buttons(st.buttons),
            switches(std::move(st.switches)),
            axis(std::move(st.axis)),
            timestamp(st.timestamp),
            num_readouts(st.num_readouts),
            use_for_mouse(st.use_for_mouse) { }

        ctrl_status_t(const ctrl_status_t& st) : num_buttons(st.num_buttons),
            buttons(st.buttons),
            switches(st.switches.size()),
            axis(st.axis.size()),
            timestamp(st.timestamp),
            num_readouts(st.num_readouts),
            use_for_mouse(st.use_for_mouse)
        {
            std::copy(st.axis.cbegin(), st.axis.cend(), axis.begin());
            std::copy(st.switches.cbegin(), st.switches.cend(), switches.begin());
        }
    };

    struct ctrl_instance_t {
        std::vector<command_t> commands;
        ctrl_status_t status;
    };

    static std::atomic<bool> joystickThreadActive = false;
    static std::thread joystickThread;
    static HWND hAppWnd = NULL;

    void PutStringInClipboard(const std::string& str)
    {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, str.size() + 1);

        if (hMem == NULL) {
            return;
        }

        auto ptr = GlobalLock(hMem);
        if (ptr == nullptr) {
            return;
        }

        memcpy(ptr, str.c_str(), str.size() + 1);
        GlobalUnlock(hMem);
        OpenClipboard(0);
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
    }

    void SendGlobalKey(uint8_t vkey, bool up)
    {
        INPUT input;
        KEYBDINPUT& ki = input.ki;

        input.type = INPUT_KEYBOARD;
        ki.wVk = 0;
        ki.wScan = MapVirtualKeyA(vkey, MAPVK_VK_TO_VSC);
        ki.dwFlags = up ? (KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE) : KEYEVENTF_SCANCODE;
        ki.time = 0;
        ki.dwExtraInfo = NULL;
        SendInput(1, &input, sizeof(INPUT));
    }

    HWND & GetTargetWindow(const PDAInjectorOptions & options)
    {
        if (hAppWnd == NULL) {
            hAppWnd = FindWindowA(NULL, options.app.window.c_str());
        }
        return hAppWnd;
    }

    void joystickThreadFcn(const PDAInjectorOptions& options)
    {
        using namespace winrt;
        using namespace Windows::Foundation;
        using namespace Windows::Gaming::Input;

        std::shared_mutex mutex;
        std::atomic<bool> controllers_updated = false;

        appMousePos.x = 0;
        appMousePos.y = 0;

        winrt::init_apartment();

        std::vector<RawGameController> controllers;
        std::unordered_map<hstring, ctrl_instance_t> ctrl_instances;

        auto handle_new_ctrl = [&](const RawGameController& ctrl)
        {
            const auto key = ctrl.NonRoamableId();
            const auto vid = ctrl.HardwareVendorId();
            const auto pid = ctrl.HardwareProductId();

            const auto num_buttons = ctrl.ButtonCount();
            const auto num_switches = ctrl.SwitchCount();
            const auto num_axis = ctrl.AxisCount();

            ctrl_instance_t instance{ {}, ctrl_status_t{num_buttons, num_switches, num_axis} };

            for (const auto& cmd : options.joystick.commands) {
                if (cmd.vid == vid && cmd.pid == pid) {
                    if (cmd.type == type_t::button_cmd && cmd.button_id < num_buttons) {
                        instance.commands.push_back(cmd);
                    }
                    else if (cmd.type == type_t::switch_cmd && cmd.button_id < num_switches) {
                        instance.commands.push_back(cmd);
                    }
                }
            }

            if (options.joystick.mouse_enabled) {
                if (options.joystick.mouse_vid == vid && options.joystick.mouse_pid == pid) {
                    instance.status.use_for_mouse = true;

                    command_t cmd;

                    cmd.button_id = options.joystick.mouse_button;
                    cmd.button_trigger_state = 1;
                    cmd.target = target_t::app;
                    cmd.trigger_vkey = VKEY_MOUSE_LBUTTON;
                    cmd.type = type_t::button_cmd;

                    instance.commands.push_back(cmd);
                }
            }

            if (!options.joystick.debug && instance.commands.empty() && instance.commands.empty() && instance.status.use_for_mouse == false) {
                return;
            }

            ctrl.GetCurrentReading(instance.status.buttons, instance.status.switches, instance.status.axis);

            std::unique_lock lock(mutex);
            controllers.push_back(ctrl);
            ctrl_instances.emplace(std::make_pair(key, std::move(instance)));
            controllers_updated = true;
        };

        for (auto const& ctrl : RawGameController::RawGameControllers()) {
            handle_new_ctrl(ctrl);
        }

        RawGameController::RawGameControllerAdded([&](IInspectable const& /* sender */, RawGameController const& ctrl)
            {
                handle_new_ctrl(ctrl);
            });

        RawGameController::RawGameControllerRemoved([&](IInspectable const& /* sender */, RawGameController const& ctrl)
            {
                std::unique_lock lock(mutex);
                if (const auto it = std::find(controllers.begin(), controllers.end(), ctrl); it != controllers.end()) {
                    controllers_updated = true;
                    controllers.erase(it);
                }
            });

        while (joystickThreadActive) {
            std::shared_lock lock(mutex);

            if (controllers_updated) {
                controllers_updated = false;
            }

            for (const auto& ctrl : controllers)
            {
                const auto key = ctrl.NonRoamableId();

                auto& instance = ctrl_instances[key];
                auto& status = instance.status;
                const auto prev_status = status;

                status.timestamp = ctrl.GetCurrentReading(status.buttons, status.switches, status.axis);
                status.num_readouts += 1;


                if (instance.status.use_for_mouse) {
                    const auto new_x = appMousePos.x + std::round(status.axis[options.joystick.mouse_x_axis] * 20.0 - 10.0);
                    const auto new_y = appMousePos.y + std::round(status.axis[options.joystick.mouse_y_axis] * 20.0 - 10.0);

                    if (new_x != appMousePos.x || new_y != appMousePos.y) {
                        appLastMouseMove = time(nullptr);
                    }

                    appMousePos.x = std::clamp(static_cast<long>(new_x), 0l, XC_WIDTH);
                    appMousePos.y = std::clamp(static_cast<long>(new_y), 0l, XC_HEIGHT);

                    if (status.buttons[options.joystick.mouse_button]) {
                        HWND targetWindow = GetTargetWindow(options);
                        if (targetWindow != NULL) {
                            SendMouseToBestTarget(targetWindow, appMousePos, WM_MOUSEMOVE, MK_LBUTTON);
                        }
                    }
                }

                if (status.timestamp == prev_status.timestamp || status.num_readouts < 10) {
                    continue; // no change, same reading
                }

                if (options.joystick.debug) {
                    for (int i = 0; i < status.num_buttons; i++) {
                        if (status.buttons[i] != prev_status.buttons[i]) {

                            const auto debug_text = fmt::format("Name: {}\nBTN Command: {:04X}:{:04X}:B:{:02d}:{:d}:??:A\n\n{}",
                                winrt::to_string(ctrl.DisplayName()),
                                ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                i, status.buttons[i],
                                "Press YES to copy the command into clipboard");

                            const auto result = MessageBoxA(FindWindowA(NULL, "Condor version 2.2.0"),
                                debug_text.c_str(), "PDA Injector Controller Debug",
                                MB_ICONINFORMATION | MB_YESNOCANCEL);

                            if (result == IDYES) {
                                PutStringInClipboard(fmt::format("{:04X}:{:04X}:B:{:02d}:{:d}:??:A",
                                    ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                    i, status.buttons[i]));
                            }
                        }
                    }

                    for (unsigned i = 0; i < status.switches.size(); i++) {
                        if (status.switches[i] != prev_status.switches[i]) {
                            const auto sw = std::clamp(static_cast<int>(status.switches[i]), 0, 8);
                            const auto debug_text = fmt::format("Name: {}\nDirection: {}\nSW Command: {:04X}:{:04X}:S:{:02d}:{:d}:??:A\n\n{}",
                                winrt::to_string(ctrl.DisplayName()),
                                switch_pos_names[sw],
                                ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                i, sw,
                                "Press YES to copy the command into clipboard");

                            const auto result = MessageBoxA(FindWindowA(NULL, "Condor version 2.2.0"),
                                debug_text.c_str(), "PDA Injector Controller Debug",
                                MB_ICONINFORMATION | MB_YESNOCANCEL);

                            if (result == IDYES) {
                                PutStringInClipboard(fmt::format("{:04X}:{:04X}:S:{:02d}:{:d}:??:A",
                                    ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                    i, status.buttons[i]));
                            }
                        }
                    }
                }
                else
                {
                    for (const auto& cmd : instance.commands) {

                        if (cmd.type == type_t::button_cmd) {
                            if (prev_status.buttons[cmd.button_id] == status.buttons[cmd.button_id]) {
                                continue;
                            }

                            const auto message = (status.buttons[cmd.button_id] == cmd.button_trigger_state) ? WM_KEYDOWN : WM_KEYUP;

                            if (cmd.target == target_t::app) {

                                HWND& targetWindow = GetTargetWindow(options);
                                if (!targetWindow) {
                                    continue;
                                }


                                bool result = false;

                                if (cmd.trigger_vkey == VKEY_MOUSE_LBUTTON) {
                                    result = SendMouseToBestTarget(targetWindow, appMousePos, message == WM_KEYUP ? WM_LBUTTONUP : WM_LBUTTONDOWN, 0);
                                }
                                else {
                                    result = PostMessageA(targetWindow, message, cmd.trigger_vkey, 0);
                                }

                                if (!result) {
                                    targetWindow = NULL;
                                }
                            }
                            else {
                                SendGlobalKey(cmd.trigger_vkey, message == WM_KEYUP);
                            }
                        }
                        else if (cmd.type == type_t::switch_cmd) {
                            if (prev_status.switches[cmd.button_id] == status.switches[cmd.button_id]) {
                                continue;
                            }

                            UINT message = 0;

                            if (prev_status.switches[cmd.button_id] == static_cast<GameControllerSwitchPosition>(cmd.switch_trigger_state)) {
                                message = WM_KEYUP;
                            } else if (status.switches[cmd.button_id] == static_cast<GameControllerSwitchPosition>(cmd.switch_trigger_state)) {
                                message = WM_KEYDOWN;
                            } else {
                                continue;
                            }

                            if (cmd.target == target_t::app) {

                                HWND& targetWindow = GetTargetWindow(options);
                                if (!targetWindow) {
                                    continue;
                                }

                                if (!PostMessageA(targetWindow, message, cmd.trigger_vkey, 0)) {
                                    targetWindow = NULL;
                                }
                            }
                            else {
                                SendGlobalKey(cmd.trigger_vkey, message == WM_KEYUP);
                            }
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void StartThread(const PDAInjectorOptions & options)
    {
        joystickThreadActive = true;
        joystickThread = std::thread(joystickThreadFcn, options);
    }

    void StopThread()
    {
        if (joystickThread.joinable()) {
            joystickThread.join();
        }
    }

    std::optional<command_t> ParseCommand(const std::string& cmd_str)
    {
        std::regex cmd_regex(R"(^([0-9a-fA-F]{4}):([0-9a-fA-F]{4}):([BS]):(\d+):([0-9]):([0-9a-fA-FxX]+|@\w):([AC])$)");
        std::smatch match;

        if (!std::regex_match(cmd_str, match, cmd_regex)) {
            return std::nullopt;
        }

        command_t cmd;

        cmd.vid = std::stoi(match[1].str(), nullptr, 16) & 0x0FFFF;
        cmd.pid = std::stoi(match[2].str(), nullptr, 16) & 0x0FFFF;
        cmd.type = match[3].str() == "S" ? type_t::switch_cmd : type_t::button_cmd;
        cmd.button_id = std::stoi(match[4].str());
        cmd.button_trigger_state = std::stoi(match[5].str()) != 0;
        const auto vk_str = match[6].str();
        if (vk_str.size() == 2 && vk_str[0] == '@') {
            cmd.trigger_vkey = static_cast<uint8_t>(vk_str[1]);
        } else {
            cmd.trigger_vkey = std::stoi(vk_str, nullptr, 16) & 0x0FF;
        }
        cmd.target = match[7].str() == "C" ? target_t::condor : target_t::app;

        return cmd;
    }

}; // namespace joystick