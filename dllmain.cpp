/*
Condor 2 PDA Injector
Copyright(C) 2024 Piotr Gertz

This program is free software; you can redistribute it and /or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
*/

#include <Windows.h>
#include "resource.h"
#include "PDA-Injector.h"
#include "joystick.h"

#include "inih/cpp/INIReader.h"
#include "fmt/format.h"


HBITMAP waitingForXCSoarBMP = NULL;
HBITMAP backgroundBMP = NULL;
HBITMAP captureBMP = NULL;

PDAInjectorOptions pdaOptions;

extern HMODULE hTrueDX11_43Module;

BOOL LoadSettings(PDAInjectorOptions& settings, std::string ini_path)
{
    INIReader reader(ini_path);

    if (reader.ParseError() < 0) {
        return FALSE;
    }

    // [pda]
    settings.pda.enabled = reader.GetBoolean("pda", "enabled", true);
    settings.pda.swap_colours = reader.GetBoolean("pda", "swap_colours", true);
    settings.pda.show_waiting_screen = reader.GetBoolean("pda", "show_waiting_screen", true);

    // [app]
    settings.app.window = reader.GetString("app", "window", "XCSoar");
    settings.app.pass_input = reader.GetBoolean("app", "pass_input", false);
    settings.app.pass_all_with_shift = reader.GetBoolean("app", "pass_all_with_shift", false);

    for (int i = 0; i < 32; i++) {
        std::string option_name = "pass_key_" + std::to_string(i + 1);
        const auto vk_code_str = reader.GetString("app", option_name, "");
        if (vk_code_str.empty()) {
            break;
        }

        char* map_to = nullptr;
        const auto vk_code = strtoul(vk_code_str.c_str(), &map_to, 16);
        if (vk_code > 0 && vk_code < 256) {
            auto dst_vk_code = vk_code;

            if (map_to != nullptr && *map_to == ':') {
                dst_vk_code = strtoul(map_to + 1, nullptr, 16);
            }

            settings.app.pass_keys[static_cast<uint8_t>(vk_code)] = static_cast<uint8_t>(dst_vk_code);
        }
    }

    settings.joystick.enabled = reader.GetBoolean("joystick", "enabled", false);
    settings.joystick.debug = reader.GetBoolean("joystick", "debug", false);

    if (settings.joystick.enabled) {
        for (int i = 0; i < 128; i++) {
            std::string option_name = "command_" + std::to_string(i + 1);
            const auto btn_str = reader.GetString("joystick", option_name, "");
            if (btn_str.empty()) {
                break;
            }

            const auto btn_command_opt = joystick::ParseCommand(btn_str);
            if (btn_command_opt == std::nullopt) {
                MessageBoxA(NULL, fmt::format("Failed to parse joystick command:\n{}", btn_str).c_str(),
                    "PDA Injector", MB_ICONWARNING);
                continue;
            }

            settings.joystick.commands.emplace_back(btn_command_opt.value());
        }
    }

    return TRUE;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
#ifdef DX11PROXY
        hTrueDX11_43Module = LoadLibraryA("d3dx11_43.original.dll");
        if (!hTrueDX11_43Module) {
            throw "Failed to load d3dx11_43.original.dll";
        }
#endif
        waitingForXCSoarBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP1));
        backgroundBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP2));
        captureBMP = LoadBitmap(hModule, MAKEINTRESOURCE(IDB_BITMAP3));
        
        LoadSettings(pdaOptions, "pda.ini");

        if (pdaOptions.joystick.enabled == true) {
            joystick::StartThread(pdaOptions);
        }

        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        DeleteObject(waitingForXCSoarBMP);
        DeleteObject(backgroundBMP);
        DeleteObject(captureBMP);

        if (pdaOptions.joystick.enabled == true) {
            joystick::StopThread();
        }


#ifdef DX11PROXY
        FreeLibrary(reinterpret_cast<HMODULE>(hTrueDX11_43Module));
#endif
        break;
    }
    return TRUE;
}

