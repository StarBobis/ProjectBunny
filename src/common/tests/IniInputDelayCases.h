#pragma once

#include <cstdint>
#include <string_view>

namespace migoto::tests {

// Both backends load these sections through their normal configuration loader.
inline constexpr char inputDelayIni[] =
	"global $gate=0\nglobal $held=2\nglobal $presses=0\nglobal $releases=0\n"
	"global $step=99\nglobal $invalid=0\nglobal $negative=0\n"
	"[KeyDelayHold]\nkey=H\ntype=hold\ndelay=4*2\ndelay=999\nrelease_delay=0x6\n"
	"condition=$gate\n$held=7\nrun=CommandListDelay\n"
	"[CommandListDelay]\n$presses=$presses+1\npost $releases=$releases+1\n"
	"[KeyDelayCycle]\nkey=A\nkey=C\nback=B\ntype=cycle\nsmart=false\ndelay=8\nrelease_delay=6\n$step=0,1,2\n"
	"[KeyDelayInvalid]\nkey=I\ndelay=$missing_delay\n$invalid=1\n"
	"[KeyDelayNegative]\nkey=N\ndelay=-1\n$negative=1\n";

template<class Frame, class Check>
void CheckInputDelayFrames(Frame frame, Check check)
{
	// The first successful hold crosses a 64-bit clock rollover.
	constexpr std::uint64_t start = UINT64_MAX - 13;
	auto expect = [&](unsigned time, std::string_view keys, bool gate,
		int held, int presses, int releases, int step, const char *description) {
		frame(start + time, keys, gate);
		check(held, presses, releases, step, description);
	};
	expect(0, "H", true, 2,0,0,99, "first sample starts press timer");
	expect(7, "", true, 2,0,0,99, "short pulse is cancelled");
	expect(8, "H", false, 2,0,0,99, "second press starts with false condition");
	expect(15, "H", true, 2,0,0,99, "press is still pending one millisecond before deadline");
	expect(16, "H", true, 7,1,0,99, "condition is read at the inclusive press deadline");
	expect(17, "", true, 7,1,0,99, "first up starts release delay");
	expect(22, "H", false, 7,1,0,99, "repress during release keeps the original hold");
	expect(23, "", false, 7,1,0,99, "next release starts from its own sample");
	expect(28, "", false, 7,1,0,99, "release remains pending before deadline");
	expect(29, "A", false, 2,1,1,99, "release restores while condition is false");
	expect(32, "AB", false, 2,1,1,99, "cycle back starts independently");
	expect(37, "AB", false, 2,1,1,0, "forward matures without waiting for back");
	expect(40, "BC", false, 2,1,1,2, "back matures while alternate forward starts");
	expect(43, "ABC", false, 2,1,1,2, "short release cannot rearm first forward key");
	expect(48, "ABC", false, 2,1,1,0, "alternate forward matures on its own deadline");
	expect(55, "HIN", false, 2,1,1,0, "second hold begins with a closed gate");
	expect(63, "HIN", false, 2,1,1,0, "blocked mature press is consumed");
	expect(99, "HIN", true, 2,1,1,0, "condition change while held cannot replay a consumed press");
}

} // namespace migoto::tests
