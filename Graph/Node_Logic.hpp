#pragma once

#include "Node.hpp"

#include <cassert>
#include <cstdint>


namespace exc {

class LogicAny
	: public InputPortConfig<Any, Any, Any, Any, Any, Any>,
	public OutputPortConfig<Any>
{
public:
	LogicAny() {
		lastActivated = -1;
		GetInput<0>().AddObserver(this);
		GetInput<1>().AddObserver(this);
		GetInput<2>().AddObserver(this);
		GetInput<3>().AddObserver(this);
		GetInput<4>().AddObserver(this);
		GetInput<5>().AddObserver(this);
	}

	void Update() override final {
		if (lastActivated >= 0) {
			InputPort<Any>* port = static_cast<InputPort<Any>*>(GetInput(lastActivated));
			GetOutput<0>().Set(port->Get());
			lastActivated = -1;
		}
	}

	void Notify(InputPortBase* sender) {
		for (ptrdiff_t i = 0; i < 6; i++) {
			if (sender == GetInput(i)) {
				lastActivated = i;
			}
		}
	}


	static std::string Info_GetName() {
		return "Any:Forward any input to the output";
	}

	static const std::vector<std::string>& Info_GetInputNames() {
		static std::vector<std::string> names = {
			"In1",
			"In2",
			"In3",
			"In4",
			"In5",
			"In6"
		};
		return names;
	}
	static const std::vector<std::string>& Info_GetOutputNames() {
		static std::vector<std::string> names = {
			"Out"
		};
		return names;
	}
private:
	intptr_t lastActivated;
};



class LogicAll
	: public InputPortConfig<void, Any, Any, Any, Any, Any, Any>,
	public OutputPortConfig<Any>
{
public:
	LogicAll() {
		activationMap = 0;
		lastActivated = -1;
		GetInput<0>().AddObserver(this);
		GetInput<1>().AddObserver(this);
		GetInput<2>().AddObserver(this);
		GetInput<3>().AddObserver(this);
		GetInput<4>().AddObserver(this);
		GetInput<5>().AddObserver(this);
		GetInput<6>().AddObserver(this);
	}

	void Update() override final {
		if (lastActivated >= 0 && activationMap == 0b11'1111) {
			InputPort<Any>* port = static_cast<InputPort<Any>*>(GetInput(lastActivated + 1));
			GetOutput<0>().Set(port->Get());
		}
	}

	void Notify(InputPortBase* sender) {
		if (sender == GetInput(0)) {
			activationMap = 0;
		}
		else {
			for (ptrdiff_t i = 0; i < 6; i++) {
				if (sender == GetInput(i + 1)) {
					activationMap |= (1 << i);
					lastActivated = i;
				}
			}
			Update();
		}
	}


	static std::string Info_GetName() {
		return "All:Forward last input when all inputs were triggered at least once";
	}

	static const std::vector<std::string>& Info_GetInputNames() {
		static std::vector<std::string> names = {
			"Reset",
			"In1",
			"In2",
			"In3",
			"In4",
			"In5",
			"In6"
		};
		return names;
	}
	static const std::vector<std::string>& Info_GetOutputNames() {
		static std::vector<std::string> names = {
			"Out"
		};
		return names;
	}
private:
	intptr_t lastActivated;
	uint32_t activationMap;
};


} // namespace exc
