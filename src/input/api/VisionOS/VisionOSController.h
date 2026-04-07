#pragma once

#include "input/api/VisionOS/VisionOSControllerProvider.h"
#include "input/api/Controller.h"

class VisionOSController : public Controller<VisionOSControllerProvider>
{
	friend class VisionOSControllerProvider;

   public:
	VisionOSController();
	std::string_view api_name() const override
	{
		static_assert(to_string(InputAPI::VisionOS) == "VisionOS");
		return to_string(InputAPI::VisionOS);
	}

	InputAPI::Type api() const override { return InputAPI::VisionOS; }

	bool is_connected() override { return true; }

	bool has_axis() const override { return true; }

	std::string get_button_name(uint64 button) const override;

   protected:
	using base_type = Controller<VisionOSControllerProvider>;
	ControllerState raw_state() override;
};