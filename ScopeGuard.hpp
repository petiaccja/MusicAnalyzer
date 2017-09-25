#pragma once



struct ScopeGuard {
public:
	ScopeGuard() { enabled = false; }
	ScopeGuard(std::function<void()> func) : func(func), enabled(true) {}
	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard(ScopeGuard&& rhs) { 
		enabled = rhs.enabled; 
		func = std::move(rhs.func);
		rhs.enabled = false;
	}
	ScopeGuard& operator=(const ScopeGuard&) = delete;
	ScopeGuard& operator=(ScopeGuard&& rhs) {
		Destroy();
		enabled = rhs.enabled;
		func = rhs.func; 
		rhs.enabled = false; 
		return *this;
	}
	~ScopeGuard() {
		Destroy();
	}
	std::function<void()> func;
	bool enabled = false;
private:
	void Destroy() {
		if (enabled) {
			func();
		}
	}
};

template <class Func>
ScopeGuard MakeScopeGuard(Func func) {
	return ScopeGuard{ func };
}