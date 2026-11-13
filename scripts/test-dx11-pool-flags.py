"""CPU regression for the production flag propagation methods.

Run inside an x64 MSVC developer shell. The two production methods and their validation helper are
extracted; the reduced resource model excludes COM allocation and INI parsing.
The full DirectX11 build separately verifies the real declarations/callers.
"""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = (ROOT / 'src/DirectX11/CommandList.cpp').read_bytes().decode('latin1')
    methods = '\n'.join(method(source, signature) for signature in (
        'static bool CanAddResourceFlags(', 'bool CustomResource::AddFlags(', 'bool CustomResourcePool::PropagateFlags('
    ))
    harness = r'''
#include <d3d11.h>
#include <vector>
#include <cassert>
#include <string>
// Only overlay rendering and enum-name formatting are replaced here.
constexpr int LOG_WARNING = 0;
enum class CustomResourceBindFlags {};
constexpr int CustomResourceBindFlagNames = 0;
static int warnings = 0;
static void LogOverlayW(int, const wchar_t*, ...) { ++warnings; }
static std::wstring lookup_enum_bit_names(int, CustomResourceBindFlags) { return L"flags"; }
class CustomResourcePool;
class CustomResource {
public:
    CustomResourcePool* pool = nullptr;
    std::wstring name = L"test";
    D3D11_BIND_FLAG bind_flags = static_cast<D3D11_BIND_FLAG>(0);
    D3D11_RESOURCE_MISC_FLAG misc_flags = static_cast<D3D11_RESOURCE_MISC_FLAG>(0);
    bool AddFlags(D3D11_BIND_FLAG, D3D11_RESOURCE_MISC_FLAG, bool recursive = true);
};
struct PoolElement { CustomResource* resource; };
class CustomResourcePool {
public:
    CustomResource* resource_template = nullptr;
    CustomResourcePool* source_pool = nullptr;
    std::vector<PoolElement> elements;
    bool PropagateFlags(D3D11_BIND_FLAG, D3D11_RESOURCE_MISC_FLAG);
};
'''
    tests = r'''
int main() {
    CustomResource standalone;
    standalone.AddFlags(D3D11_BIND_VERTEX_BUFFER, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    standalone.AddFlags(D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    assert(standalone.bind_flags == (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE));
    assert(standalone.misc_flags == D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    CustomResource prototype, first, second;
    CustomResourcePool pool;
    pool.resource_template = &prototype;
    pool.elements = {{&first}, {nullptr}, {&second}};
    prototype.pool = first.pool = second.pool = &pool;
    first.bind_flags = D3D11_BIND_INDEX_BUFFER;
    prototype.AddFlags(D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    assert(first.bind_flags == (D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE));
    assert(second.bind_flags == D3D11_BIND_SHADER_RESOURCE);
    assert(prototype.misc_flags == D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);
    assert(first.misc_flags == prototype.misc_flags && second.misc_flags == prototype.misc_flags);
    second.AddFlags(D3D11_BIND_VERTEX_BUFFER, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    assert((prototype.bind_flags & D3D11_BIND_VERTEX_BUFFER) != 0);
    assert((first.bind_flags & D3D11_BIND_VERTEX_BUFFER) != 0);
    const auto flags = first.bind_flags;
    second.AddFlags(D3D11_BIND_VERTEX_BUFFER, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    assert(first.bind_flags == flags);
    pool.elements.clear();
    prototype.AddFlags(D3D11_BIND_STREAM_OUTPUT, static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    assert((prototype.bind_flags & D3D11_BIND_STREAM_OUTPUT) != 0);
    const auto zero = static_cast<D3D11_RESOURCE_MISC_FLAG>(0);
    CustomResource cb;
    cb.misc_flags = static_cast<D3D11_RESOURCE_MISC_FLAG>(
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    assert(cb.AddFlags(D3D11_BIND_CONSTANT_BUFFER, zero));
    assert(cb.misc_flags == 0 && cb.bind_flags == D3D11_BIND_CONSTANT_BUFFER);
    assert(cb.AddFlags(D3D11_BIND_CONSTANT_BUFFER, zero));
    assert(!cb.AddFlags(D3D11_BIND_SHADER_RESOURCE, zero));
    assert(cb.bind_flags == D3D11_BIND_CONSTANT_BUFFER);
    assert(!standalone.AddFlags(D3D11_BIND_CONSTANT_BUFFER, zero));
    assert(standalone.bind_flags == (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE));
    pool.elements = {{&first}, {nullptr}, {&cb}};
    cb.pool = &pool;
    assert(!prototype.AddFlags(D3D11_BIND_VERTEX_BUFFER, zero));
    assert(cb.bind_flags == D3D11_BIND_CONSTANT_BUFFER);
    assert(warnings >= 3);
}
'''
    output = ROOT / 'build/dx11-pool-regression'
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'pool-flags.cpp'
    cpp.write_text(harness + methods + tests, encoding='utf-8')
    subprocess.run(['cl', '/nologo', '/std:c++17', '/EHsc', '/W4', str(cpp), '/Fe:pool-flags.exe'], cwd=output, check=True)
    subprocess.run([str(output / 'pool-flags.exe')], check=True)
    print('PASS: standalone/pool flags, null slots, preservation, repeated propagation, empty pool, CB conflicts')


if __name__ == '__main__':
    main()
