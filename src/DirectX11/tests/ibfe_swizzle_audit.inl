struct IbfeSwizzleAuditCase {const char* name; const char* instructions;};

void test_dx11_decompiler_public_ibfe_regression()
{
    using namespace dx11_slot_test;
    const char source[]="float4 main():SV_Target{return float4(0,0,0,1);}";
    ComPtr<ID3DBlob> template_code,errors;
    require_batch(SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&template_code,&errors)),
        "F272: actual native shader supplies a real DXBC container/signature");
    const auto* base=static_cast<const unsigned char*>(template_code->GetBufferPointer());
    const std::vector<unsigned char> original(base,base+template_code->GetBufferSize());
    const IbfeSwizzleAuditCase cases[]={
        {"scalar-literal","ibfe r0.x, l(4), l(0), l(6)\nitof r0.x, r0.x\nmul o0.x, r0.x, l(0.1)\nmov o0.yzw, l(0,0,0,1.0)\nret\n"},
        {"vector-literal-2","ibfe r0.xy, l(4,4,4,4), l(0,0,0,0), l(2,4,6,7)\nitof r0.xy, r0.xy\nmul o0.xy, r0.xy, l(0.1,0.1,0.1,0.1)\nmov o0.zw, l(0,0,0,1.0)\nret\n"},
        {"scalar-broadcast-3","ibfe r0.xyz, l(4), l(0), l(3)\nitof r0.xyz, r0.xyz\nmul o0.xyz, r0.xyz, l(0.1,0.1,0.1,0.1)\nmov o0.w, l(1.0)\nret\n"},
        {"vector-literal-4","ibfe r0.xyzw, l(4,4,4,4), l(0,0,0,0), l(2,4,6,7)\nitof r0.xyzw, r0.xyzw\nmul o0.xyzw, r0.xyzw, l(0.1,0.1,0.1,0.1)\nret\n"},
        {"register-scalar","ftoi r0.xyzw, l(2.0,4.0,6.0,7.0)\nibfe r1.x, l(4), l(0), r0.y\nitof r1.x, r1.x\nmul o0.x, r1.x, l(0.1)\nmov o0.yzw, l(0,0,0,1.0)\nret\n"},
        {"register-swizzle","ftoi r0.xyzw, l(2.0,4.0,6.0,7.0)\nibfe r1.xyzw, l(4,4,4,4), l(0,0,0,0), r0.wzyx\nitof r1.xyzw, r1.xyzw\nmul o0.xyzw, r1.xyzw, l(0.1,0.1,0.1,0.1)\nret\n"}};
    BatchDevice device;
    for(const auto& test:cases) {
        const std::string assembly="ps_5_0\ndcl_output o0.xyzw\ndcl_temps 2\n"+std::string(test.instructions);
        std::vector<char> input(assembly.begin(),assembly.end());
        auto code=assembler(&input,original);
        ComPtr<ID3D11PixelShader> native_shader,recompiled_shader;
        require_batch(SUCCEEDED(device.native->CreatePixelShader(code.data(),code.size(),nullptr,&native_shader)),
            "F272: native WARP accepts the real IBFE DXBC before decompilation");
        std::vector<unsigned char> text;
        require_batch(SUCCEEDED(disassembler(&code,&text,nullptr)),"F272: actual native disassembly and precision conversion");
        const std::string native_assembly(text.begin(),text.end());
        require_batch(native_assembly.find("ibfe ")!=std::string::npos,"F272: native instruction stream retains the tested bitfield operation");
        DecompilerSettings settings;
        ParseParameters params{}; params.bytecode=code.data(); params.decompiled=native_assembly.c_str();
        params.decompiledSize=native_assembly.size(); params.G=&settings;
        bool patched=false,failed=false; std::string model;
        const auto hlsl=DecompileBinaryHLSL(params,patched,model,failed);
        require_batch(!failed && !hlsl.empty(),"F272: public decompiler produces its actual IBFE result");
        const auto prefix=std::string("f272-")+test.name;
        {std::ofstream output(prefix+".asm",std::ios::binary); output.write(native_assembly.data(),native_assembly.size()); output.close(); require_batch(output.good(),"F272: archive native assembly witness");}
        {std::ofstream output(prefix+".hlsl",std::ios::binary); output.write(hlsl.data(),hlsl.size()); output.close(); require_batch(output.good(),"F272: archive actual public decompiler output");}
        std::fprintf(stderr,"F272: native-valid %s IBFE must recompile after public decompilation\n",test.name); std::fflush(stderr);
        ComPtr<ID3DBlob> rebuilt;
        const auto hr=D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&rebuilt,&errors);
        if(FAILED(hr) && errors) std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);
        require_batch(SUCCEEDED(hr) && rebuilt,"F272: generated scalar/vector cast types are valid native HLSL");
        require_batch(SUCCEEDED(device.native->CreatePixelShader(rebuilt->GetBufferPointer(),rebuilt->GetBufferSize(),nullptr,&recompiled_shader)),
            "F272: WARP accepts the recompiled IBFE shader");
        const auto expected=render_dx11_audit_constant_pixel(device,native_shader.Get());
        require_batch(expected[0]!=0 && render_dx11_audit_constant_pixel(device,recompiled_shader.Get())==expected,
            "F272: scalar, broadcast, literal-vector and register masks retain their native rendered bytes");
    }
}
