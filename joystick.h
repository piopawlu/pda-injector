#pragma once

#include <optional>
#include <vector>
#include <string>

struct PDAInjectorOptions;

namespace joystick {
	enum class target_t {
		app = 0,
		condor = 1
	};

	struct button_command_t {
		unsigned short vid = 0;
		unsigned short pid = 0;
		unsigned char trigger_vkey = 0;
		
		int button_id = 0;
		bool button_trigger_state = true;
		target_t target = target_t::app;
	};


	std::optional<button_command_t> ParseCommand(const std::string& cmd);

	void StartThread(const PDAInjectorOptions& options);
	void StopThread();
};