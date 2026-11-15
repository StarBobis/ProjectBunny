// Compile production ShaderRegex once in the regression host so the locator
// probe executes its actual private implementation, including the red build.
#include "../ShaderRegex.cpp"

bool probe_dx11_shader_regex_declaration_anchor(std::string* text, size_t* anchor)
{
    return find_dcl_end(text, anchor);
}
