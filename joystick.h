#pragma once

#include <optional>
#include <vector>
#include <string>

struct PDAInjectorOptions;

#define VKEY_MOUSE_LBUTTON 0xFF

namespace joystick {
	enum class target_t {
		app = 0,
		condor = 1
	};

	enum class type_t {
		button_cmd = 0,
		switch_cmd = 1
	};

	struct command_t {
		unsigned short vid = 0;
		unsigned short pid = 0;
		unsigned char trigger_vkey = 0;
		
		int button_id = 0;
		bool button_trigger_state = true;
		int switch_trigger_state = 0;
		target_t target = target_t::app;
		type_t type = type_t::button_cmd;
	};


	std::optional<command_t> ParseCommand(const std::string& cmd);

	void StartThread(const PDAInjectorOptions& options);
	void StopThread();
};