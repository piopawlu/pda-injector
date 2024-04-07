#include "PDA-Injector.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <vector>
#include <thread>
#include <shared_mutex>
#include <regex>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#include "fmt/include/fmt/format.h"

#include "joystick.h"

using winrt::Windows::Gaming::Input::GameControllerSwitchPosition;

namespace joystick {

    struct ctrl_status_t {
        int num_buttons = 0;
        std::array<bool, 128> buttons;
        std::vector<GameControllerSwitchPosition> switches;
        std::vector<double> axis;

        uint64_t timestamp = 0;
        uint64_t num_readouts = 0;

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
            num_readouts(st.num_readouts) { }

        ctrl_status_t(const ctrl_status_t& st) : num_buttons(st.num_buttons),
            buttons(st.buttons),
            switches(st.switches.size()),
            axis(st.axis.size()),
            timestamp(st.timestamp),
            num_readouts(st.num_readouts)
        {
            std::copy(st.axis.cbegin(), st.axis.cend(), axis.begin());
            std::copy(st.switches.cbegin(), st.switches.cend(), switches.begin());
        }
    };

    struct ctrl_instance_t {
        std::vector<button_command_t> commands;
        ctrl_status_t status;
    };

    static std::atomic<bool> joystickThreadActive = false;
    static std::thread joystickThread;
    static HWND hCondorWnd = NULL;
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

    HWND & GetTargetWindow(target_t target, const PDAInjectorOptions & options)
    {

        if (target == target_t::condor) {
            if (hCondorWnd == NULL) {
                hCondorWnd = FindWindowA(NULL, "Condor version 2.2.0");
            }
            return hCondorWnd;
        }
        else if (target == target_t::app)
        {
            if (hAppWnd == NULL) {
                hAppWnd = FindWindowA(NULL, options.app.window.c_str());
            }
            return hAppWnd;
        }
        else
        {
            throw std::runtime_error("Not implemented!");
        }
    }

    void joystickThreadFcn(const PDAInjectorOptions& options)
    {
        using namespace winrt;
        using namespace Windows::Foundation;
        using namespace Windows::Gaming::Input;

        std::shared_mutex mutex;
        std::atomic<bool> controllers_updated = false;

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

            for (const auto& cmd : options.joystick.button_commands) {
                if (cmd.vid == vid && cmd.pid == pid && cmd.button_id < num_buttons) {
                    instance.commands.push_back(cmd);
                }
            }

            if (!options.joystick.debug && instance.commands.empty()) {
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
                const auto key = ctrl.NonRoamableId(); //(ctrl.HardwareVendorId() << 16 | ctrl.HardwareProductId());

                auto& instance = ctrl_instances[key];
                auto& status = instance.status;
                const auto prev_status = status;

                status.timestamp = ctrl.GetCurrentReading(status.buttons, status.switches, status.axis);
                status.num_readouts += 1;
                if (status.timestamp == prev_status.timestamp || status.num_readouts < 10) {
                    continue; // no change, same reading
                }

                if (options.joystick.debug) {
                    for (int i = 0; i < status.num_buttons; i++) {
                        if (status.buttons[i] != prev_status.buttons[i]) {

                            const auto debug_text = fmt::format("Name: {}\nCommand: {:04X}:{:04X}:{:02d}:{:d}:??:A\n\n{}",
                                winrt::to_string(ctrl.DisplayName()),
                                ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                i, status.buttons[i],
                                "Press YES to copy the command into clipboard");

                            const auto result = MessageBoxA(FindWindowA(NULL, "Condor version 2.2.0"),
                                debug_text.c_str(), "PDA Injector Controller Debug",
                                MB_ICONINFORMATION | MB_YESNOCANCEL);

                            if (result == IDYES) {
                                PutStringInClipboard(fmt::format("{:04X}:{:04X}:{:02d}:{:d}:??:A",
                                    ctrl.HardwareVendorId(), ctrl.HardwareProductId(),
                                    i, status.buttons[i]));
                            }
                        }
                    }
                }
                else
                {
                    for (const auto& cmd : instance.commands) {
                        if (prev_status.buttons[cmd.button_id] == status.buttons[cmd.button_id]) {
                            continue;
                        }

                        HWND & targetWindow = GetTargetWindow(cmd.target, options);
                        if (!targetWindow) {
                            continue;
                        }

                        const auto message = (status.buttons[cmd.button_id] == cmd.button_trigger_state) ? WM_KEYDOWN : WM_KEYUP;

                        if (!PostMessageA(targetWindow, message, cmd.trigger_vkey, 0)) {
                            targetWindow = NULL;
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

    std::optional<button_command_t> ParseCommand(const std::string& cmd_str)
    {
        std::regex cmd_regex(R"(^([0-9a-fA-F]{4}):([0-9a-fA-F]{4}):(\d+):([01]):([0-9a-fA-FxX]+):([AC])$)");
        std::smatch match;

        if (!std::regex_match(cmd_str, match, cmd_regex)) {
            return std::nullopt;
        }

        button_command_t cmd;

        cmd.vid = std::stoi(match[1].str(), nullptr, 16) & 0x0FFFF;
        cmd.pid = std::stoi(match[2].str(), nullptr, 16) & 0x0FFFF;
        cmd.button_id = std::stoi(match[3].str());
        cmd.button_trigger_state = std::stoi(match[4].str()) != 0;
        cmd.trigger_vkey = std::stoi(match[5].str(), nullptr, 16) & 0x0FF;
        cmd.target = match[6].str() == "C" ? target_t::condor : target_t::app;

        return cmd;
    }

}; // namespace joystick