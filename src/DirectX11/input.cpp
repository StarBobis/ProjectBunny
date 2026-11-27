#include "Input.h"

#include <Xinput.h>
#include <array>
#include <vector>
#include <algorithm>
#include <sstream>
#include <climits>
#include <unordered_map>

#include "log.h"
#include "util.h"
#include "IniInputChord.h"
#include "IniHandler.h"

// Set a function pointer to the xinput get state call. By default, set it to
// XInputGetState() in whichever xinput we are linked to (xinput9_1_0.dll). If
// the d3dx.ini is using the guide button we will try to switch to either
// xinput 1.3 or 1.4 to get access to the undocumented XInputGetStateEx() call.
// We can't rely on these existing on Win7 though, so if we fail to load them
// don't treat it as fatal and continue using the original one.
static HMODULE xinput_lib;
typedef DWORD (WINAPI *tXInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
static tXInputGetState _XInputGetState = XInputGetState;

static void SwitchToXinpuGetStateEx()
{
	tXInputGetState XInputGetStateEx;

	if (xinput_lib)
		return;

	// 3DMigoto is linked against xinput9_1_0.dll, but that version does
	// not export XInputGetStateEx to get the guide button. Try loading
	// xinput 1.3 and 1.4, which both support this functionality.
	xinput_lib = LoadLibrary(L"xinput1_3.dll");
	if (xinput_lib) {
		LogInfo("Loaded xinput1_3.dll for guide button support\n");
	} else {
		xinput_lib = LoadLibrary(L"xinput1_4.dll");
		if (xinput_lib) {
			LogInfo("Loaded xinput1_4.dll for guide button support\n");
		} else {
			LogInfo("ERROR: Unable to load xinput 1.3 or 1.4: Guide button will not be available\n");
			return;
		}
	}

	// Unnamed and undocumented exports FTW
	LPCSTR XInputGetStateExOrdinal = (LPCSTR)100;
	XInputGetStateEx = (tXInputGetState)GetProcAddress(xinput_lib, XInputGetStateExOrdinal);
	if (!XInputGetStateEx) {
		LogInfo("ERROR: Unable to get XInputGetStateEx: Guide button will not be available\n");
		return;
	}

	_XInputGetState = XInputGetStateEx;
}

// VS2013 BUG WORKAROUND: Make sure this class has a unique type name!
class KeyParseError: public exception {} keyParseError;

namespace {
// Cache only the state used by CheckState (the high bit). Zero denotes a key
// or combination that this dispatch pass has not yet observed.
struct InputPollSnapshot {
	VirtualKeyStateReader read_key;
	bool reuse;
	bool reject_inactive_presses;
	std::vector<unsigned char>& combinations;
	migoto::VirtualKeySnapshot virtual_keys;
};

thread_local InputPollSnapshot* input_poll_snapshot = nullptr;
thread_local std::vector<unsigned char> input_combination_states;
std::unordered_map<std::wstring, size_t> input_combination_indexes;

class InputPollScope {
	InputPollSnapshot* previous;
public:
	explicit InputPollScope(InputPollSnapshot& current) : previous(input_poll_snapshot)
	{
		input_poll_snapshot = &current;
	}
	~InputPollScope() { input_poll_snapshot = previous; }
};
}

void InputListener::UpEvent(HackerDevice *device)
{
}

// -----------------------------------------------------------------------------

InputCallbacks::InputCallbacks(InputCallback down_cb, InputCallback up_cb,
		void *private_data) :
	down_cb(down_cb),
	up_cb(up_cb),
	private_data(private_data)
{}

void InputCallbacks::DownEvent(HackerDevice *device)
{
	if (down_cb)
		return down_cb(device, private_data);
}

void InputCallbacks::UpEvent(HackerDevice *device)
{
	if (up_cb)
		return up_cb(device, private_data);
}


// -----------------------------------------------------------------------------

InputAction::InputAction(InputButton *button, shared_ptr<InputListener> listener) :
		last_state(false),
		button(button),
		listener(listener)
	{}

InputAction::~InputAction()
{
	delete button;
}

bool InputAction::CheckButtonState()
{
	auto* snapshot = input_poll_snapshot;
	if (!snapshot || !snapshot->reuse || snapshot_index >= snapshot->combinations.size())
		return button->CheckState();
	auto& value = snapshot->combinations[snapshot_index];
	if (!value)
		value = button->CheckState() ? 1 : 2;
	return value == 1;
}

bool InputAction::Dispatch(HackerDevice *device)
{
	if (input_poll_snapshot && input_poll_snapshot->reject_inactive_presses && disable_scope == InputDisableScope::MODS &&
		!last_state && listener->RejectsPress()) {
		// Observe the shared hardware state even for a rejected press so
		// appearing while the key is held cannot create a new press event.
		last_state = CheckButtonState();
		return last_state;
	}

	bool state = CheckButtonState();

	if (state == last_state)
		return false;

	if (state)
		listener->DownEvent(device);
	else
		listener->UpEvent(device);

	last_state = state;

	return true;
}


// -----------------------------------------------------------------------------

VKInputButton::VKInputButton(const wchar_t *keyName) :
	invert(false)
{
	vkey = migoto::ParseKeyboardButton(keyName, invert);
	if (vkey < 0)
		throw keyParseError;
}

// The check for < 0 is a little odd.  The reason to use this form is because
// the call can also set the low bit in different situations that can theoretically
// result in non-zero, but top bit not set. This form ensures we only test the
// actual key bit.

bool VKInputButton::CheckState()
{
	auto* snapshot = input_poll_snapshot;
	if (!snapshot)
		return ((GetAsyncKeyState(vkey) < 0) ^ invert);
	auto down = [snapshot](int vk) { return snapshot->read_key(vk) < 0; };
	return (snapshot->reuse ? snapshot->virtual_keys.Down(vkey, down) : down(vkey)) ^ invert;
}


// -----------------------------------------------------------------------------
// The RepeatAction is to allow for auto-repeat on hunting operations.
// Regular user inputs, and not all hunting operations are suitable for auto-
// repeat.  These are only created for operations that desire auto-repeat,
// otherwise the VKInputButton or XInputButton is used.

// For Dispatch, we have no need to be called as often as we are, that's just
// an artifact of where we get processing time, from the Draw() calls made by the game.
// To trim this down to a sensible human-oriented, keyboard input type time, we'll
// use the GetTickCount64 to skip processing.  The reason to add this limiter is
// to make auto-repeat slow enough to be usable, and consistent.

// TODO: Determine if an alternate thread can properly provide time. That would make
// it possible to simply have the OS call us as desired.

RepeatingInputAction::RepeatingInputAction(InputButton *button, shared_ptr<InputListener> listener, int repeat) :
	repeatRate(repeat),
	InputAction(button, listener)
{}

bool RepeatingInputAction::Dispatch(HackerDevice *device)
{
	if (!migoto::InputRepeatDue(lastTick, GetTickCount64(), repeatRate))
		return false;

	bool state = CheckButtonState();

	// Only allow auto-repeat for down events.
	if (state || (state != last_state))
	{
		if (state)
			listener->DownEvent(device);
		else
			listener->UpEvent(device);

		lastTick = GetTickCount64();
		last_state = state;

		return true;
	}

	return false;
}

DelayedInputAction::DelayedInputAction(InputButton *button, shared_ptr<InputListener> listener, int delay_down, int delay_up) :
	delay_down(delay_down),
	delay_up(delay_up),
	effective_state(false),
	state_change_time(0),
	InputAction(button, listener)
{}

bool DelayedInputAction::Dispatch(HackerDevice *device)
{
	ULONGLONG now = GetTickCount64();
	bool state = CheckButtonState();

	if (state != last_state)
		state_change_time = now;
	last_state = state;

	if (state != effective_state) {
		if (state && ((now - state_change_time) >= delay_down)) {
			effective_state = state;
			listener->DownEvent(device);
			return true;
		} else if (!state && ((now - state_change_time) >= delay_up)) {
			effective_state = state;
			listener->UpEvent(device);
			return true;
		}
	}

	return false;
}

// -----------------------------------------------------------------------------

struct XInputState_t {
	XINPUT_STATE state;
	bool connected;
};
static XInputState_t XInputState[4];

bool XInputButton::_CheckState(int controller)
{
	XINPUT_GAMEPAD *gamepad = &XInputState[controller].state.Gamepad;

	if (!XInputState[controller].connected)
		return false; // Don't invert if it's not connected

	if (button && (gamepad->wButtons & button))
		return true ^ invert;
	if (left_trigger && (gamepad->bLeftTrigger >= left_trigger))
		return true ^ invert;
	if (right_trigger && (gamepad->bRightTrigger >= right_trigger))
		return true ^ invert;

	return false ^ invert;
}

static EnumName_t<wchar_t *, WORD> XInputButtons[] = {
	{L"DPAD_UP", XINPUT_GAMEPAD_DPAD_UP},
	{L"DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
	{L"DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT},
	{L"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
	{L"START", XINPUT_GAMEPAD_START},
	{L"BACK", XINPUT_GAMEPAD_BACK},
	{L"LEFT_THUMB", XINPUT_GAMEPAD_LEFT_THUMB},
	{L"RIGHT_THUMB", XINPUT_GAMEPAD_RIGHT_THUMB},
	{L"LEFT_SHOULDER", XINPUT_GAMEPAD_LEFT_SHOULDER},
	{L"RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER},
	{L"A", XINPUT_GAMEPAD_A},
	{L"B", XINPUT_GAMEPAD_B},
	{L"X", XINPUT_GAMEPAD_X},
	{L"Y", XINPUT_GAMEPAD_Y},
	{L"GUIDE", 0x400}, /* Requires undocumented XInputGetStateEx call in xinput 1.3 / 1.4 */
};

// This function is parsing strings with formats such as:
//
// XB_DPAD_DOWN           - dpad down on any controller
// xb1_left_trigger > 128 - left trigger half way on 1st controller
//
// I originally wrote this using regular expressions rather than C style
// pointer arithmetic, but it looks like MSVC's implementation may be buggy
// (either that or I have a very precise 100% reproducable memory corruption
// issue), which isn't that surprising given that regular expressions are
// uncommon in the Windows world. Feel free to rewrite this in a cleaner way.
XInputButton::XInputButton(const wchar_t *keyName) :
	controller(-1),
	button(0),
	left_trigger(0),
	right_trigger(0),
	invert(false)
{
	int i, threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	BYTE *trigger;

	if (!_wcsnicmp(keyName, L"no_", 3)) {
		invert = true;
		keyName += 3;
	}

	if (_wcsnicmp(keyName, L"XB", 2))
		throw keyParseError;
	keyName += 2;

	if (*keyName >= L'1' && *keyName <= L'4') {
		controller = *keyName - L'1';
		keyName++;
	}

	if (*keyName != L'_')
		throw keyParseError;
	keyName++;

	for (i = 0; i < ARRAYSIZE(XInputButtons); i++) {
		if (!_wcsicmp(keyName, XInputButtons[i].name)) {
			button = XInputButtons[i].val;
			break;
		}
	}

	if (!_wcsicmp(keyName, L"GUIDE"))
		SwitchToXinpuGetStateEx();

	if (button)
		return;

	if (!_wcsnicmp(keyName, L"LEFT_TRIGGER", 11)) {
		trigger = &left_trigger;
		keyName += 12;
	} else if (!_wcsnicmp(keyName, L"RIGHT_TRIGGER", 12)) {
		trigger = &right_trigger;
		keyName += 13;
	} else
		throw keyParseError;

	while (*keyName == L' ')
		keyName++;

	if (*keyName == L'>') {
		keyName++;
		while (*keyName == L' ')
			keyName++;
		threshold = _wtoi(keyName);
	}

	*trigger = min(threshold + 1, 255);
}

bool XInputButton::CheckState()
{
	int i;

	if (controller != -1)
		return _CheckState(controller);

	for (i = 0; i < 4; i++) {
		if (_CheckState(i))
			return true;
	}

	return false;
}

InputButtonList::InputButtonList(const wchar_t *keyName)
{
	const bool valid = migoto::VisitInputChord(keyName, [this](const wchar_t *token) {
		try {
			buttons.push_back(new VKInputButton(token));
		} catch (KeyParseError) {
			try {
				buttons.push_back(new XInputButton(token));
			} catch (KeyParseError) {
				return false;
			}
		}
		return true;
	});
	if (!valid) {
		clear();
		throw keyParseError;
	}
}

void InputButtonList::clear()
{
	vector<InputButton*>::iterator i;

	for (i = buttons.begin(); i < buttons.end(); i++)
		delete *i;

	buttons.clear();
}

InputButtonList::~InputButtonList()
{
	clear();
}

bool InputButtonList::CheckState()
{
	vector<InputButton*>::iterator i;

	for (i = buttons.begin(); i < buttons.end(); i++) {
		if (!(*i)->CheckState())
			return false;
	}

	return true;
}

static std::vector<class InputAction *> actions;

struct NamedInputBinding {
	InputBindingInfo info;
	shared_ptr<InputListener> listener;
	InputDisableScope scope;
};
static std::vector<NamedInputBinding> named_bindings;

std::vector<InputBindingInfo> ListInputBindings()
{
	std::vector<InputBindingInfo> result;
	for (const auto& binding : named_bindings)
		result.push_back(binding.info);
	return result;
}

bool InvokeInputBinding(HackerDevice* device, const std::wstring& id, bool release)
{
	for (const auto& binding : named_bindings) {
		if (_wcsicmp(id.c_str(), binding.info.id.c_str()) != 0)
			continue;
		if (G->disable_input && binding.scope >= InputDisableScope::MODS &&
			G->input_disable_scope >= binding.scope)
			return false;
		auto listener = binding.listener;
		if (release)
			listener->UpEvent(device);
		else
			listener->DownEvent(device);
		return true;
	}
	return false;
}

void RegisterInputAction(std::unique_ptr<InputAction> action, InputDisableScope disable_scope)
{
	if (!action)
		return;
	action->disable_scope = disable_scope == InputDisableScope::INVALID ? InputDisableScope::ALL : disable_scope;
	actions.push_back(action.get());
	action.release();
}

bool RegisterKeyBinding(LPCWSTR iniKey, const wchar_t *keyName,
		shared_ptr<InputListener> listener, int auto_repeat, int down_delay,
		int up_delay, InputDisableScope disable_scope, const wchar_t *action_id)
{
	std::unique_ptr<InputAction> action;
	std::unique_ptr<InputButton> button;

	// We could potentially only use the InputButtonList here, but that
	// does not work with some of our backwards compatibility key names
	// that contain spaces ("Num blah", "Prnt Scrn"), so we still try to
	// parse the keyName as a single key first.
	try {
		button = std::make_unique<VKInputButton>(keyName);
	} catch (KeyParseError) {
		try {
			button = std::make_unique<XInputButton>(keyName);
		} catch (KeyParseError) {
			try {
				button = std::make_unique<InputButtonList>(keyName);
			} catch (KeyParseError) {
				LogOverlayW(LOG_WARNING, L"WARNING: UNABLE TO PARSE KEY BINDING %ls=%ls\n",
						iniKey, keyName);
				return false;
			}
		}
	}

	if (auto_repeat)
		action = std::make_unique<RepeatingInputAction>(button.get(), listener, auto_repeat);
	else if (down_delay || up_delay)
		action = std::make_unique<DelayedInputAction>(button.get(), listener, down_delay, up_delay);
	else
		action = std::make_unique<InputAction>(button.get(), listener);
	button.release();
	std::wstring signature(keyName);
	std::transform(signature.begin(), signature.end(), signature.begin(), ::towlower);
	const auto index = input_combination_indexes.try_emplace(std::move(signature), input_combination_indexes.size());
	action->snapshot_index = index.first->second;

	LogInfoW(L"  %s=%s\n", iniKey, keyName);
	RegisterInputAction(std::move(action), disable_scope);
	if (action_id)
		named_bindings.push_back({{action_id, keyName}, std::move(listener), disable_scope});
	return true;
}

bool RegisterIniKeyBinding(LPCWSTR app, LPCWSTR iniKey,
		InputCallback down_cb, InputCallback up_cb, int auto_repeat,
		void *private_data, InputDisableScope disable_scope)
{
	shared_ptr<InputCallbacks> callbacks = make_shared<InputCallbacks>(down_cb, up_cb, private_data);
	wchar_t keyName[MAX_PATH];

	const std::wstring action_id = std::wstring(app) + L"." + iniKey;
	if (!GetIniString(app, iniKey, 0, keyName, MAX_PATH)) {
		named_bindings.push_back({{action_id, L""}, std::move(callbacks), disable_scope});
		return false;
	}

	return RegisterKeyBinding(iniKey, keyName, callbacks, auto_repeat, 0, 0, disable_scope, action_id.c_str());
}

// Format a key binding from the ini file into a user friendly manner, intended
// for cases where we might want to show a key binding on the overlay.
// "no_modifiers VK_F10" -> "F10"
// "ctrl alt no_shift VK_F10" -> "Ctrl+Alt+F10"
wstring user_friendly_ini_key_binding(LPCWSTR app, LPCWSTR iniKey)
{
	wchar_t keyName[MAX_PATH];
	wstring ret;

	if (!GetIniString(app, iniKey, 0, keyName, MAX_PATH))
		return L"<None>";

	std::wistringstream tokens(keyName);
	std::wstring token;

	while (std::getline(tokens, token, L' ')) {
		if (!_wcsnicmp(token.c_str(), L"no_", 3))
			continue;

		if (!_wcsnicmp(token.c_str(), L"VK_", 3))
			token.erase(0, 3);

		if (token.empty())
			continue;

		if (!ret.empty())
			ret += L'+';

		token[0] = towupper(token[0]);
		std::transform(token.begin() + 1, token.end(), token.begin() + 1, ::towlower);

		ret += token;
	}

	return ret;
}

void ClearKeyBindings()
{
	input_combination_indexes.clear();
	std::vector<class InputAction *>::iterator i;

	for (i = actions.begin(); i != actions.end(); i++)
		delete *i;

	actions.clear();
	named_bindings.clear();
}

static bool IsAdditionalForegroundWindow(HWND hwnd)
{
	if (G->additionalForegroundWindowTitle.empty())
		return false;

	wchar_t title[512] = {};
	GetWindowTextW(hwnd, title, _countof(title));
	return wcscmp(title, G->additionalForegroundWindowTitle.c_str()) == 0;
}

static bool CheckForegroundWindow()
{
	DWORD pid;

	if (!G->check_foreground_window)
		return true;

	HWND hwnd = GetForegroundWindow();
	if (!hwnd)
		return false;

	GetWindowThreadProcessId(hwnd, &pid);

	if (pid == GetCurrentProcessId())
		return true;

	// Allow additional foreground window matched by title
	return IsAdditionalForegroundWindow(hwnd);
}

bool DispatchInputEvents(HackerDevice *device)
{
	static time_t last_time = 0;
	time_t now = time(NULL);
	int j;

	if (!CheckForegroundWindow())
		return false;

	for (j = 0; j < 4; j++) {
		// Stagger polling controllers that were not connected last
		// frame over four seconds to minimise performance impact,
		// which has been observed to be extremely significant.
		if (!XInputState[j].connected && ((now == last_time) || (now % 4 != j)))
			continue;

		XInputState[j].connected =
			(_XInputGetState(j, &XInputState[j].state) == ERROR_SUCCESS);
	}

	last_time = now;

	return DispatchRegisteredInputActions(device);
}

static bool DispatchInputActions(HackerDevice *device)
{
	bool input_processed = false;
	for (InputAction* action : actions) {
		if (G->disable_input && action->disable_scope >= InputDisableScope::MODS &&
			G->input_disable_scope >= action->disable_scope)
			continue;
		input_processed |= action->Dispatch(device);
	}
	return input_processed;
}

bool DispatchRegisteredInputActions(HackerDevice *device, VirtualKeyStateReader read_key)
{
	const bool reuse = G->ModRuntimeOptimizationsEnabled() &&
		G->optimize_hotkey_dispatch.load(std::memory_order_relaxed) && !G->analyse_frame && !gLogDebug;
	if (!reuse && read_key == GetAsyncKeyState && !input_poll_snapshot)
		return DispatchInputActions(device);

	std::vector<unsigned char> nested_states;
	auto& combinations = input_poll_snapshot ? nested_states : input_combination_states;
	if (reuse)
		combinations.assign(input_combination_indexes.size(), 0);
	InputPollSnapshot snapshot{read_key, reuse, reuse && !Dx11LogEnabled(LogVerbosity::INFO), combinations};
	InputPollScope scope(snapshot);
	return DispatchInputActions(device);
}

size_t CountRegisteredInputActions(InputDisableScope disable_scope)
{
	return static_cast<size_t>(std::count_if(actions.begin(), actions.end(),
		[disable_scope](const InputAction* action) { return action->disable_scope == disable_scope; }));
}

static std::wstring NormalizeInputSourcePath(std::wstring path)
{
	if (path.empty())
		return {};
	std::replace(path.begin(), path.end(), L'/', L'\\');
	const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (!required)
		return {};
	std::wstring absolute(required, L'\0');
	const DWORD written = GetFullPathNameW(path.c_str(), required, absolute.data(), nullptr);
	if (!written || written >= required)
		return {};
	absolute.resize(written);
	while (!absolute.empty() && absolute.back() == L'\\')
		absolute.pop_back();
	return absolute;
}

bool IsInputPathWithinDirectory(const std::wstring& filename, const std::wstring& directory)
{
	const std::wstring source = NormalizeInputSourcePath(filename);
	const std::wstring root = NormalizeInputSourcePath(directory);
	if (root.empty() || root.size() > INT_MAX || source.size() <= root.size() || source[root.size()] != L'\\')
		return false;
	return CompareStringOrdinal(source.data(), static_cast<int>(root.size()), root.data(),
		static_cast<int>(root.size()), TRUE) == CSTR_EQUAL;
}
