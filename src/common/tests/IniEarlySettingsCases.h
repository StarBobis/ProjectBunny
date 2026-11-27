#pragma once

#include "ExpressionFunctions.h"
#include <array>
#include <iomanip>
#include <limits>
#include <ostream>

namespace migoto::tests {

// Each expression would sample and select the opposite flag if a Mod variable
// were visible. The file is loaded both before and after its declaration exists.
inline constexpr char earlySystemInput[] =
	"check_foreground_window=random(1) >= $early_off\n"
	"clear_unknown_settings=random(1) < $early_off\n"
	"[Input]\ninput=random(1) < $early_off\ninput_disable_mode=all\n";
inline constexpr char earlyRepeat[] = "repeat_rate=random(1) + $early_off\n";
inline constexpr char earlyDeclaration[] = "global $early_off=0\n";

// Called after input has been initialized. Each reader recognizes only its
// expected draw from the same random stream. A swapped pair changes real settings.
inline float WriteEarlyReloadOptions(std::ostream &out)
{
	auto seed = [] { SeedExpressionRandom(817u - 0x9e3779b9u * GetExpressionRandomSampleCount(), 41); };
	seed();
	std::array<float, 6> values;
	for (auto &value : values)
		value = RandomExpressionValue(1);
	seed();
	out << std::setprecision(std::numeric_limits<float>::max_digits10)
		<< "[System]\ncheck_foreground_window=random(1) === " << values[0]
		<< "\nclear_unknown_settings=random(1) === " << values[1]
		<< "\n[Input]\ninput=random(1)\n[Rendering]\nmod_runtime_optimizations=random(1) === " << values[2]
		<< "\nexpression_short_circuit=random(1) === " << values[3]
		<< "\n[Hunting]\nhunting=1\nnext_pixelshader=F23\nrepeat_rate=5 * (random(1) === " << values[4]
		<< ")\n[Constants]\nglobal $early_sample=random(1)\n";
	return values.back();
}

} // namespace migoto::tests
