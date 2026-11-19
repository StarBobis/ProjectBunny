#pragma once

#include <wrl.h>
#include <string>
#include <atomic>
#include <memory>

namespace Profiling {
	enum class Mode {
		NONE = 0,
		SUMMARY,
		TOP_COMMAND_LISTS,
		TOP_COMMANDS,
		CTO_WARNING,

		INVALID, // Must be last
	};

	class Overhead {
	public:
		LARGE_INTEGER cpu = {};
		unsigned count = 0, hits = 0;

		void clear();
	};

    using CollectionToken = std::shared_ptr<const void>;
	struct State {
		LARGE_INTEGER start_time = {};
        bool started = false;
        CollectionToken collection{};
	};

    void start(State *state);
    void end(State *state, Overhead *overhead, bool count = false, bool hit = false);

    CollectionToken GetCollectionToken();
    bool IsCurrentCollection(const CollectionToken& token);

    // Completed samples belong to the window and mode observed at entry.
    class Sample {
        Overhead* overhead_;
        LARGE_INTEGER start_time_{};
        CollectionToken token_;
        bool active_ = false;
        bool count_;
    public:
        explicit Sample(Overhead& overhead, bool count = true);
        ~Sample() noexcept;
        void Finish(bool hit) noexcept;
        Sample(const Sample&) = delete;
        Sample& operator=(const Sample&) = delete;
    };

    template<class T>
    static inline typename T::iterator lookup_map(T &map, typename T::key_type key, Profiling::Overhead *overhead)
    {
        Sample sample(*overhead);
        auto ret = map.find(key);
        sample.Finish(ret != map.end());
        return ret;
    }

	void update_txt();
	void update_cto_warning(bool warn);
	void clear();

	extern std::atomic<Mode> mode;
	extern Overhead present_overhead;
	extern Overhead overlay_overhead;
	extern Overhead draw_overhead;
	extern Overhead region_tracking_overhead;
	extern Overhead map_overhead;
	extern Overhead pipeline_invalidation_overhead;
	extern Overhead readback_map_overhead;
	extern Overhead expression_om_query_overhead;
	extern Overhead expression_cb_query_overhead;
	extern Overhead draw_gate_uncached;
	extern Overhead draw_gate_memo;
	extern Overhead draw_gate_ib;
	extern Overhead draw_gate_vb;
	extern Overhead draw_gate_srv;
	extern Overhead draw_gate_candidates;
	extern Overhead draw_gate_pre_entry_miss;
	extern Overhead draw_gate_post_entry_miss;
	extern Overhead iniparams_upload_overhead;
	extern Overhead hash_tracking_overhead;
	extern Overhead stat_overhead;
	extern Overhead shaderregex_overhead;
	extern Overhead cursor_overhead;
	extern std::wstring text;
	extern std::wstring cto_warning;
	extern INT64 interval;
	extern bool freeze;

	extern Overhead shader_hash_lookup_overhead;
	extern Overhead shader_reload_lookup_overhead;
	extern Overhead shader_original_lookup_overhead;
	extern Overhead shaderoverride_lookup_overhead;
	extern Overhead texture_handle_info_lookup_overhead;
	extern Overhead textureoverride_lookup_overhead;
	extern Overhead resource_pool_lookup_overhead;
    extern Overhead texture_override_fuzzy_match_overhead;
    extern Overhead texture_override_candidates_lookup_overhead;

	extern unsigned resource_full_copies;
	extern unsigned resource_reference_copies;
	extern unsigned inter_device_copies;
	extern unsigned msaa_resolutions;
	extern unsigned buffer_region_copies;
	extern unsigned views_cleared;
	extern unsigned resources_created;
	extern unsigned resource_pool_swaps;
	extern unsigned max_copies_per_frame_exceeded;
	extern unsigned injected_draw_calls;
	extern unsigned skipped_draw_calls;
	extern unsigned max_executions_per_frame_exceeded;
	extern unsigned iniparams_updates;

}

// Also used before a config reload destroys the registered command objects.
void clear_command_list_profiling();
