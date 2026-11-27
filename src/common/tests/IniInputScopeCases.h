#pragma once

#include <string>

namespace migoto::tests {

struct ScopeFile {
	const char *path;
	const char *body;
};

inline constexpr ScopeFile scopeFiles[] = {
	{"loader.ini", "namespace=RenamedLoader\n[Include]\ninclude=Mods\\child.ini\ninclude_recursive=More\n"},
	{"Mods/direct.ini", "namespace=DirectAlias\n[KeyDirect]\nkey=D\n$direct_value=1\n"},
	{"Mods/hold.ini", "namespace=UnrelatedAlias\n[KeyHold]\nkey=H\ntype=hold\n$held_value=7\n"},
	{"Mods/cycle.ini", "[KeyCycle]\ntype=cycle\nkey=C\nback=B\nsmart=false\n$cycle_value=2,3\n"},
	{"Mods/child.ini", "[KeyChild]\nkey=N\n$child_value=1\n"},
	{"Mods/duplicate.ini", "namespace=DirectAlias\n[KeyDirect]\nkey=Q\n$direct_value=9\n"},
	{"More/deep.ini", "[KeyDeep]\nkey=J\n$deep_value=1\n"},
};

inline std::string ScopeConfig(const std::string &prefix, const std::string &input, const std::string &mode, const std::string &settings = "d3dx_user.ini")
{
	return "[Input]\ninput=" + input + "\ninput_disable_mode=" + mode + "\ntoggle_input=T\n"
		"[Include]\nuser_config=" + settings + "\ninclude=" + prefix + "loader.ini\ninclude=" + prefix + "Mods\\direct.ini\n"
		"include_recursive=" + prefix + "Mods\ninclude=" + prefix + "Mods\\hold.ini\n"
		"[Constants]\nglobal $root_value=0\nglobal $direct_value=0\nglobal $held_value=3\n"
		"global $cycle_value=99\nglobal $child_value=0\nglobal $deep_value=0\n"
		"[KeyRoot]\nkey=R\n$root_value=1\n";
}

// Frames pass through each backend's actual config/registration and dispatch.
// R and D are tool keys. H,C,B,N,J came from recursive traversal.
template<class Frame, class Reload, class Check>
void CheckScopeFrames(Frame frame, Reload reload, Check check)
{
	frame("QRDHCNJ");
	check(1,1,3,99,0,0, "mods mode leaves root and direct-first keys enabled; recursive and nested keys stay idle");
	frame("");
	frame("THCNJ");
	check(1,1,7,2,1,1, "privileged toggle enables recursive keys during the same frame");
	frame("H");
	frame("TH");
	frame("");
	check(1,1,7,2,1,1, "disabled mod hold defers physical release");
	frame("TB");
	check(1,1,3,3,1,1, "re-enable observes deferred release and cycle back");
	frame("");
	frame("T");
	frame("");
	reload("on", "ALL");
	frame("QRDHCNJ");
	check(0,0,3,99,0,0, "all mode blocks every ordinary key after reload and keeps runtime disabled");
	frame("TRDHCNJ");
	check(1,1,7,2,1,1, "re-enable in all mode delivers keys held while blocked");
	frame("");
	reload("off", "mods");
	frame("H");
	check(0,0,7,99,0,0, "reload preserves enabled runtime despite input=off");
	frame("");
}

} // namespace migoto::tests
