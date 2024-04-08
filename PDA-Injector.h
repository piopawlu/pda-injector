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

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <array>
#include <vector>

#include "joystick.h"

struct PDAInjectorOptions {
	// [pda] section
	struct {
		int  page = 2; // 0 - map, 1 - MC, 2 - nav , 3 - thermal/wind
		bool enabled = true;
		bool show_waiting_screen = true;
		bool swap_colours = true; //XCSoar returns data with red and blue channels swapped...
	} pda;

	// [app] section
	struct {
		std::string window = "XCSoar";
		bool pass_input = true;
		bool pass_all_with_shift = true;
		std::array<uint8_t, 256> pass_keys;
	} app;

	// [joystick] section
	struct {
		bool enabled = true;
		bool debug = false;
		
		std::vector<joystick::command_t> commands;
	} joystick;

	PDAInjectorOptions() {
		std::fill(app.pass_keys.begin(), app.pass_keys.end(), 0);
	}
};
