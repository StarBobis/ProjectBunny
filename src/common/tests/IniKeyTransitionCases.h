#pragma once

namespace migoto::tests {

// The fixtures prepend [Constants]. Each sampled button mask is dispatched
// as one frame, followed by the backend's normal transition update.
inline constexpr char keyPhaseIni[] =
	"global $v=2\nglobal $pre=0\nglobal $post=0\nglobal $saved=0\n"
	"global $trace=0\nglobal $presses=0\nglobal $releases=0\nglobal $release_value=0\n"
	"global $order=0\n[KeyZObserver]\nkey=XB2_A\ncondition=$trace==123\nrun=CommandListOrder\n"
	"[CommandListOrder]\n$order=$v\npost $order=$order*10+$v\n"
	"[KeyCurve]\nkey=XB2_Y\ntype=hold\n$v=10\ntransition=1000000\ntransition_type=cosine\nrelease_transition=2000000\n"
	"[KeyTap]\nkey=XB2_A\ntype=activate\n$v=8\nrun=CommandListTap\n"
	"[CommandListTap]\nlocal $before=$v\n$pre=$v\npre run=CommandListExplicit\n"
	"run=CommandListObserve\npost $saved=$before\npost run=CommandListExplicit\n"
	"[CommandListObserve]\nif $v==2\n$trace=$trace*10+3\nendif\n"
	"if $v==8\npost $trace=$trace*10+4\npost $post=$v\nendif\n"
	"[CommandListExplicit]\n$trace=$trace*10+1\npost $trace=$trace*10+2\n"
	"[KeyHold]\nkey=XB2_B\ntype=hold\n$v=4\nrun=CommandListHeld\n"
	"[KeyOther]\nkey=XB2_X\ntype=toggle\n$v=6\nrun=CommandListHeld\n"
	"[CommandListHeld]\n$presses=$presses+1\npost $releases=$releases+1\npost $release_value=$v\n";

struct KeyPhaseSample
{
	bool tap, hold, toggle;
	float value, presses, releases, releaseValue;
};

inline constexpr KeyPhaseSample keyPhaseSamples[] = {
	{true, false, false, 8, 0, 0, 0},
	{false, true, false, 4, 1, 0, 0},
	{false, true, true, 6, 2, 0, 0},
	{false, false, false, 8, 2, 1, 6},
	{false, false, true, 8, 2, 2, 8},
};

} // namespace migoto::tests
