// The configuration and event consumers here are the production implementations.
class CycleAuditLifetime {
public:
    KeyOverrideCycle cycle;
    explicit CycleAuditLifetime(const wchar_t* section=L"KeyF193Cycle") {cycle.ParseIniSection(section);}
    ~CycleAuditLifetime() {pending_post_command_lists.clear(); OverrideSave.Reset(nullptr);}
};

float cycle_audit_value(const wchar_t* name)
{
    return command_list_globals.at(name).fval;
}

void cycle_audit_load(AuditConfigScope& config,BatchDevice& device,const char* source,const char* includes="")
{
    require_batch(pending_post_command_lists.empty(),"F193: previous event post list has completed before loading another case");
    OverrideSave.Reset(nullptr);
    config.load(source,0,true,includes);
    RunCommandList(device.device,device.wrapper.get(),&G->constants_command_list,nullptr,false);
}

void cycle_audit_flush(BatchDevice& device,bool complete_timed=false)
{
    if(complete_timed) {
        // Backdate only this fixture's real scheduled records; use the normal
        // update engine without sleeps or a process-wide clock replacement.
        for(auto& item:CurrentTransition.vars)
            if(item.second.time>0) item.second.activation_time-=1000;
    }
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(pending_post_command_lists.empty(),"F193: production transition update consumes every queued post list");
}

void cycle_audit_run(BatchDevice& device,const wchar_t* name)
{
    RunCommandList(device.device,device.wrapper.get(),&explicitCommandListSections.at(name).command_list,nullptr,false);
}

class CycleAuditIncludeFile {
public:
    std::filesystem::path path;
    CycleAuditIncludeFile(const std::filesystem::path& base,const char* source):path(base/L"f193-namespace.ini")
    {
        require_batch(!std::filesystem::exists(path),"F193: included namespace fixture has an exclusive name");
        std::ofstream output(path,std::ios::binary); output<<source; output.close();
        require_batch(output.good(),"F193: complete real included INI source");
    }
    ~CycleAuditIncludeFile() {std::error_code error; std::filesystem::remove(path,error);}
};

void test_f193()
{
    IniParameterStateScope saved; AuditConfigScope config; BatchDevice device;
    cycle_audit_load(config, device, migoto::tests::keyPhaseIni);
    {
        KeyOverride tap(KeyOverrideType::ACTIVATE), hold(KeyOverrideType::HOLD), toggle(KeyOverrideType::TOGGLE);
        tap.ParseIniSection(L"KeyTap"); hold.ParseIniSection(L"KeyHold"); toggle.ParseIniSection(L"KeyOther");
        KeyOverride observer(KeyOverrideType::ACTIVATE);
        observer.ParseIniSection(L"KeyZObserver");
        bool held = false, toggled = false;
        for (const auto &sample : migoto::tests::keyPhaseSamples) {
            if (sample.tap) {
                tap.DownEvent(device.device);
                observer.DownEvent(device.device);
            }
            if (sample.hold && !held) hold.DownEvent(device.device);
            if (!sample.hold && held) hold.UpEvent(device.device);
            if (sample.toggle && !toggled) toggle.DownEvent(device.device);
            held = sample.hold;
            toggled = sample.toggle;
            cycle_audit_flush(device);
            require_batch(cycle_audit_value(L"$v") == sample.value && cycle_audit_value(L"$presses") == sample.presses &&
                cycle_audit_value(L"$releases") == sample.releases && cycle_audit_value(L"$release_value") == sample.releaseValue,
                "DX11 hold and toggle overlap, release runs only post before restoration");
            require_batch(cycle_audit_value(L"$pre") == 2 && cycle_audit_value(L"$post") == 8 &&
                cycle_audit_value(L"$saved") == 2 && cycle_audit_value(L"$trace") == 123412 && cycle_audit_value(L"$order") == 28,
                "DX11 pre/post order, shared local, nested inherited and explicit phases");
        }
        KeyOverride curve(KeyOverrideType::HOLD);
        curve.ParseIniSection(L"KeyCurve");
        curve.DownEvent(device.device);
        auto &record = CurrentTransition.vars.at(&command_list_globals.at(L"$v"));
        record.activation_time = GetTickCount64() - 250000;
        cycle_audit_flush(device);
        require_batch(std::abs(cycle_audit_value(L"$v") - 8.292893f) < 0.002f, "DX11 cosine quarter using scheduled scalar");
        curve.UpEvent(device.device);
        require_batch(record.target == 8 && record.time == 2000000, "DX11 release restores original while press is in flight");
        record.activation_time = GetTickCount64() - 2000001;
        cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$v") == 8 && CurrentTransition.vars.empty(), "DX11 release completes and retires");
    }
    for (const auto &test : migoto::tests::keyCycleCases) {
        cycle_audit_load(config, device, test.Config().c_str());
        CycleAuditLifetime item(L"KeyCycle");
        for (const auto &press : test.presses) {
            if (press.externalValue)
                command_list_globals.at(L"$phase").fval = *press.externalValue;
            if (press.back)
                item.cycle.BackEvent(device.device);
            else
                item.cycle.DownEvent(device.device);
            cycle_audit_flush(device);
            require_batch(cycle_audit_value(L"$phase") == press.value &&
                cycle_audit_value(L"$score") == press.score, test.name);
        }
    }
    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\n"
        "[CommandListF193A]\n$f193_trace=$f193_trace*10+1\npost $f193_post=$f193_post*10+1\n"
        "[CommandListF193B]\n$f193_trace=$f193_trace*10+2\npost $f193_post=$f193_post*10+2\n"
        "[KeyF193Cycle]\nkey=VK_F24\ntype=cycle\nsmart=false\nrun=CommandListF193A,CommandListF193B\n");
    {
        CycleAuditLifetime item;
        item.cycle.DownEvent(device.device);
        require_batch(cycle_audit_value(L"$f193_trace")==1 && cycle_audit_value(L"$f193_post")==0,
            "F193: first cycle entry runs its pre commands while post remains pending");
        CurrentTransition.UpdateTransitions(device.device);
        require_batch(cycle_audit_value(L"$f193_post")==1,"F193: first entry post commands run once");
        std::fputs("F193: second cycle entry must not run the preceding entry's command list\n",stderr); std::fflush(stderr);
        item.cycle.DownEvent(device.device);
        require_batch(cycle_audit_value(L"$f193_trace")==12,"F193: second cycle entry has only its own pre commands");
        CurrentTransition.UpdateTransitions(device.device);
        require_batch(cycle_audit_value(L"$f193_post")==12,"F193: second cycle entry has only its own post commands");
        item.cycle.DownEvent(device.device); CurrentTransition.UpdateTransitions(device.device);
        require_batch(cycle_audit_value(L"$f193_trace")==121 && cycle_audit_value(L"$f193_post")==121,
            "F193: wrapping reuses the first independent entry");
        item.cycle.BackEvent(device.device); CurrentTransition.UpdateTransitions(device.device);
        require_batch(cycle_audit_value(L"$f193_trace")==1212 && cycle_audit_value(L"$f193_post")==1212,
            "F193: reverse wrapping uses the last independent entry");
    }

    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\nglobal $f193_phase=99\n"
        "global $f193_pre_phase=-1\nglobal $f193_post_phase=-1\n"
        "[CommandListF193A]\n$f193_trace=$f193_trace*10+1\n$f193_pre_phase=$f193_phase\n"
        "post $f193_post=$f193_post*10+1\npost $f193_post_phase=$f193_phase\n"
        "[CommandListF193B]\n$f193_trace=$f193_trace*10+2\n$f193_pre_phase=$f193_phase\n"
        "post $f193_post=$f193_post*10+2\npost $f193_post_phase=$f193_phase\n"
        "[KeyF193Cycle]\nkey=VK_F24\ntype=cycle\nsmart=false\n$f193_phase=0,1,2,3\n"
        "transition=100\nrun=CommandListF193A,,CommandListF193B\n");
    {
        CycleAuditLifetime item;
        item.cycle.DownEvent(device.device);
        require_batch(cycle_audit_value(L"$f193_phase")==99 && cycle_audit_value(L"$f193_pre_phase")==99 &&
            cycle_audit_value(L"$f193_post")==0,"F193: pre sees the old value while a nonzero transition and post remain pending");
        auto transition=CurrentTransition.vars.find(&command_list_globals.at(L"$f193_phase"));
        require_batch(transition!=CurrentTransition.vars.end() && transition->second.time==100,"F193: real nonzero transition was scheduled");
        cycle_audit_flush(device,true);
        require_batch(cycle_audit_value(L"$f193_phase")==0 && cycle_audit_value(L"$f193_post_phase")==0,
            "F193: queued post observes the completed transition without clock sleeps");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device,true);
        require_batch(cycle_audit_value(L"$f193_phase")==1 && cycle_audit_value(L"$f193_trace")==1 && cycle_audit_value(L"$f193_post")==1,
            "F193: explicit blank middle run does not inherit earlier commands");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device,true);
        require_batch(cycle_audit_value(L"$f193_phase")==2 && cycle_audit_value(L"$f193_trace")==12 && cycle_audit_value(L"$f193_post")==12,
            "F193: following nonblank run remains independent");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device,true);
        require_batch(cycle_audit_value(L"$f193_phase")==3 && cycle_audit_value(L"$f193_trace")==122 && cycle_audit_value(L"$f193_post")==122,
            "F193: a shorter run list repeats its last value once per later entry");
    }

    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\n"
        "[CommandListF193Leaf1]\n$f193_trace=$f193_trace*10+1\npost $f193_post=$f193_post*10+1\n"
        "[CommandListF193Leaf2]\n$f193_trace=$f193_trace*10+2\npost $f193_post=$f193_post*10+2\n"
        "[CommandListF193Leaf3]\n$f193_trace=$f193_trace*10+3\npost $f193_post=$f193_post*10+3\n"
        "[CommandListF193Bundle]\nrun=CommandListF193Leaf1\nrun=CommandListF193Leaf2\n"
        "[CommandListF193Alias]\n"
        "[CommandListF193SetA]\nCommandListF193Alias=ref CommandListF193Bundle\n"
        "[CommandListF193SetB]\nCommandListF193Alias=ref CommandListF193Leaf3\n"
        "[KeyF193Cycle]\nkey=VK_F24\ntype=cycle\nsmart=false\nrun=CommandListF193Alias,CommandListF193Alias\n");
    {
        CycleAuditLifetime item;
        cycle_audit_run(device,L"commandlistf193seta");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_trace")==12 && cycle_audit_value(L"$f193_post")==12,
            "F193: a single cycle entry preserves all nested run commands and both inherited halves");
        cycle_audit_run(device,L"commandlistf193setb");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_trace")==123 && cycle_audit_value(L"$f193_post")==123,
            "F193: live CommandList reference changes remain visible without duplicate previous-entry callbacks");
    }

    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\nglobal $f193_phase=0\n"
        "[CommandListF193A]\n$f193_trace=$f193_trace*10+1\npost $f193_post=$f193_post*10+1\n"
        "[CommandListF193B]\n$f193_trace=$f193_trace*10+2\npost $f193_post=$f193_post*10+2\n"
        "[CommandListF193C]\n$f193_trace=$f193_trace*10+3\npost $f193_post=$f193_post*10+3\n"
        "[KeyF193Cycle]\nkey=VK_F24\ntype=cycle\nsmart=false\nwrap=false\n$f193_phase=1,2,3,4\n"
        "condition=1,1,0,1\nrun=CommandListF193A,CommandListF193Missing,CommandListF193B,CommandListF193C\n");
    {
        CycleAuditLifetime item;
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==2 && cycle_audit_value(L"$f193_trace")==1 && cycle_audit_value(L"$f193_post")==1,
            "F193: invalid run target does not reuse old commands while the current entry's parameters still apply");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==2 && cycle_audit_value(L"$f193_trace")==1,"F193: false condition retains its existing activation skip");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==4 && cycle_audit_value(L"$f193_trace")==13 && cycle_audit_value(L"$f193_post")==13,
            "F193: later valid entry does not replay failed or conditionally skipped entries");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_trace")==13 && cycle_audit_value(L"$f193_post")==13,"F193: nonwrapping cycle stops at its existing boundary");
    }

    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\nglobal $f193_phase=3\n"
        "[CommandListF193Leaf1]\n$f193_trace=$f193_trace*10+1\npost $f193_post=$f193_post*10+1\n"
        "[CommandListF193Leaf2]\n$f193_trace=$f193_trace*10+2\npost $f193_post=$f193_post*10+2\n"
        "[CommandListF193Bundle]\nrun=CommandListF193Leaf1\nrun=CommandListF193Leaf2\n"
        "[PresetF193Audit]\n$f193_phase=7\nrun=CommandListF193Bundle\n");
    {
        auto& preset=presetOverrides.at(L"presetf193audit");
        preset.Trigger(nullptr); CurrentTransition.UpdatePresets(device.device);
        require_batch(cycle_audit_value(L"$f193_trace")==12 && cycle_audit_value(L"$f193_post")==0 && pending_post_command_lists.empty(),
            "F193: preset activation retains its separate pre/deactivation-post policy and nested multiple runs");
        cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==7,"F193: preset transition still applies its own values");
        CurrentTransition.UpdatePresets(device.device);
        require_batch(cycle_audit_value(L"$f193_post")==12,"F193: preset deactivation still executes both target post runs");
        cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==3,"F193: preset deactivation restores the prior value");
    }

    cycle_audit_load(config,device,
        "global $f193_trace=0\nglobal $f193_post=0\nglobal $f193_phase=99\n"
        "[KeyF193Cycle]\nkey=VK_F24\ntype=cycle\nsmart=false\n$f193_phase=0,1\n"
        "[KeyF193Empty]\nkey=VK_F23\ntype=cycle\n");
    {
        CycleAuditLifetime item;
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        item.cycle.BackEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==0 && cycle_audit_value(L"$f193_trace")==0 && cycle_audit_value(L"$f193_post")==0,
            "F193: absent run still cycles parameter values without invoking earlier loaded command lists");
    }
    {
        CycleAuditLifetime empty(L"KeyF193Empty");
        empty.cycle.DownEvent(device.device); empty.cycle.BackEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_phase")==0 && cycle_audit_value(L"$f193_trace")==0 && cycle_audit_value(L"$f193_post")==0,
            "F193: a completely empty cycle remains a no-op");
    }

    {
        CycleAuditIncludeFile include(config.config.parent_path(),
            "namespace=F193Scope\n"
            "[CommandListA]\n$f193_trace=$f193_trace*10+4\npost $f193_post=$f193_post*10+4\n"
            "[CommandListB]\n$f193_trace=$f193_trace*10+5\npost $f193_post=$f193_post*10+5\n"
            "[KeyCycle]\nkey=VK_F24\ntype=cycle\nsmart=false\nrun=CommandListA,CommandListB\n");
        cycle_audit_load(config,device,"global $f193_trace=0\nglobal $f193_post=0\n","include=f193-namespace.ini\n");
        CycleAuditLifetime item(L"Key\\F193Scope\\Cycle");
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        item.cycle.DownEvent(device.device); cycle_audit_flush(device);
        require_batch(cycle_audit_value(L"$f193_trace")==45 && cycle_audit_value(L"$f193_post")==45,
            "F193: actual included INI namespace resolves the current entry's callbacks without cross-entry accumulation");
    }
}
