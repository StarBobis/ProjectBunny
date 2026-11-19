#include "ShaderRegex.h"
#include "CommandList.h"
#include "globals.h" // For ShaderOverride FIXME: This should be in a separate header
#include "log.h"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <string_view>

ShaderRegexGroups shader_regex_groups;
std::vector<ShaderRegexGroup*> shader_regex_group_index;
uint32_t shader_regex_hash;

static void log_pcre2_error_nonl(int err, char *fmt, ...)
{
	PCRE2_UCHAR buf[120]; // doco says "120 code units is ample"
	va_list ap;

	pcre2_get_error_message(err, buf, sizeof(buf));

	va_start(ap, fmt);
	vLogInfo(fmt, ap);
	va_end(ap);

	LogInfo(": %s\n", buf);
}

static bool get_shader_model(std::string *asm_text, std::string *shader_model)
{
	size_t shader_model_pos;

	for (
		shader_model_pos = asm_text->find("\n");
		shader_model_pos != std::string::npos && (*asm_text)[shader_model_pos + 1] == '/';
		shader_model_pos = asm_text->find("\n", shader_model_pos + 1)
	) {}

	if (shader_model_pos == std::string::npos)
		return false;

	*shader_model = asm_text->substr(shader_model_pos + 1, asm_text->find("\n", shader_model_pos + 1) - shader_model_pos - 1);
	return true;
}

static bool find_dcl_end(std::string *asm_text, size_t *dcl_end_pos)
{
	size_t anchor = std::string::npos;
	bool level9_header = false;
	char level9_stage = 0;
	for (size_t pos = 0; pos < asm_text->size();) {
		const size_t line_end = asm_text->find('\n', pos);
		const size_t end = line_end == std::string::npos ? asm_text->size() : line_end;
		std::string_view line(asm_text->data() + pos, end - pos);
		const size_t raw_first = line.find_first_not_of(" \t\r");
		if (anchor == std::string::npos && raw_first != std::string_view::npos) {
			auto comment = line.substr(raw_first);
			comment = comment.substr(0, comment.find_last_not_of(" \t\r") + 1);
			if (comment == "// Level9 shader bytecode:") level9_header = true;
		}
		line = line.substr(0, line.find("//"));
		const size_t first = line.find_first_not_of(" \t\r");
		if (first != std::string_view::npos) {
			line.remove_prefix(first);
			line = line.substr(0, line.find_last_not_of(" \t\r") + 1);
			if (anchor == std::string::npos) {
				const bool compatibility_model = line == "vs_2_0" || line == "vs_2_x" ||
					line == "ps_2_0" || line == "ps_2_x";
				if (level9_header && first != 0 && (level9_stage || compatibility_model)) {
					// Like the assembler, ignore the indented Level9 bytecode
					// until its unindented DXBC model, never treating it as dcl_.
					if (!level9_stage) level9_stage = line[0];
				} else {
					const bool model = line.size() == 6 &&
						std::string_view("pvghdc").find(line[0]) != std::string_view::npos &&
						line[1] == 's' && line[2] == '_' && line[4] == '_' &&
						line[3] >= '4' && line[3] <= '5' && line[5] >= '0' && line[5] <= '9';
					if (!model || line_end == std::string::npos || (level9_stage && line[0] != level9_stage))
						break;
					anchor = line_end;
				}
			} else if (line.substr(0, 4) == "dcl_") {
				if (line_end == std::string::npos) {
					anchor = std::string::npos;
					break;
				}
				// Keep the last declaration, including later hull-shader phases.
				anchor = line_end;
			}
		}
		if (line_end == std::string::npos)
			break;
		pos = line_end + 1;
	}

	if (anchor != std::string::npos) {
		*dcl_end_pos = anchor;
		return true;
	}
	LogInfo("WARNING: Unable to locate end of shader declarations!\n");
	return false;
}

static bool insert_declarations(std::string *asm_text, ShaderRegexDeclarations *declarations)
{
	ShaderRegexDeclarations::iterator i;
	std::string insert_str;
	size_t dcl_end;
	bool patch = false;

	if (!find_dcl_end(asm_text, &dcl_end))
		return false;

	for (i = declarations->begin(); i != declarations->end(); i++) {
		insert_str = std::string("\n") + *i;

		if (asm_text->find(insert_str + std::string("\n")) != std::string::npos)
			continue;

		asm_text->insert(dcl_end, insert_str);
		dcl_end += insert_str.size();

		patch = true;
	}

	return patch;
}

static bool find_dcl_temps(std::string *asm_text, size_t *dcl_temps_pos)
{
	// Could use regex for this as well, but given we only need to find a
	// constant string it will be more efficient to just do this:
	*dcl_temps_pos = asm_text->find("\ndcl_temps ", 0);

	if (*dcl_temps_pos == std::string::npos)
		return false;

	return true;
}

static bool get_dcl_temps(std::string *asm_text, unsigned *tmp_regs)
{
	size_t dcl_temps;
	*tmp_regs = 0;

	if (!find_dcl_temps(asm_text, &dcl_temps))
		return true;

	size_t first = dcl_temps + 11;
	size_t last = asm_text->find('\n', first);
	if (last == std::string::npos)
		last = asm_text->size();
	while (first < last && ((*asm_text)[first] == ' ' || (*asm_text)[first] == '\t' || (*asm_text)[first] == '\r'))
		first++;
	while (last > first && ((*asm_text)[last - 1] == ' ' || (*asm_text)[last - 1] == '\t' || (*asm_text)[last - 1] == '\r'))
		last--;

	const auto parsed = std::from_chars(asm_text->data() + first, asm_text->data() + last, *tmp_regs);
	if (first == last || parsed.ec != std::errc() || parsed.ptr != asm_text->data() + last ||
		*tmp_regs > D3D11_COMMONSHADER_TEMP_REGISTER_COUNT) {
		LogInfo("WARNING: Invalid dcl_temps count\n");
		return false;
	}
	LogInfo("Found dcl_temps %u\n", *tmp_regs);
	return true;
}

static bool update_dcl_temps(std::string *asm_text, size_t new_val)
{
	size_t dcl_temps, dcl_temps_end, dcl_end;
	std::string insert_str;

	if (new_val > D3D11_COMMONSHADER_TEMP_REGISTER_COUNT) {
		LogInfo("WARNING: ShaderRegex temporary-register count exceeds the shader limit\n");
		return false;
	}

	if (find_dcl_temps(asm_text, &dcl_temps)) {
		dcl_temps += 11;
		dcl_temps_end = asm_text->find("\n", dcl_temps);
		LogInfo("Updating dcl_temps %Iu\n", new_val);
		asm_text->replace(dcl_temps, dcl_temps_end - dcl_temps, std::to_string(new_val));
		return true;
	}

	if (!find_dcl_end(asm_text, &dcl_end))
		return false;

	insert_str = std::string("\ndcl_temps ") + std::to_string(new_val);
	LogInfo("Inserting dcl_temps %Iu\n", new_val);
	asm_text->insert(dcl_end, insert_str);
	dcl_end += insert_str.size();

	return true;
}

ShaderRegexPattern::ShaderRegexPattern() :
	regex(NULL),
	do_replace(false)
{
}

ShaderRegexPattern::~ShaderRegexPattern()
{
	pcre2_code_free(regex);
}

bool ShaderRegexPattern::compile(std::string *pattern)
{
	uint32_t name_table_entry_size;
	uint32_t name_table_count;
	uint32_t i;
	PCRE2_SPTR name_table;
	PCRE2_SIZE err_off;
	int err;

	// CASELESS is for compatibility with d3dcompiler_46 & 47 without
	// having to always remember to account for the dcl_constantbuffer
	// differences:
	regex = pcre2_compile((PCRE2_SPTR)pattern->c_str(),
			pattern->length(), // or PCRE2_ZERO_TERMINATED
			PCRE2_CASELESS | PCRE2_MULTILINE,
			&err, &err_off, NULL);
	if (!regex) {
		log_pcre2_error_nonl(err, "  WARNING: PCRE2 regex compilation failed at offset %u", (unsigned)err_off);
		return false;
	}

	// TODO: Use callback to confirm that JIT does actually get used, as in
	// some cases pcre2 can fall back to using the slower interpreter
	pcre2_jit_compile(regex, 0);

	pcre2_pattern_info(regex, PCRE2_INFO_NAMECOUNT, &name_table_count);
	pcre2_pattern_info(regex, PCRE2_INFO_NAMEENTRYSIZE, &name_table_entry_size);
	pcre2_pattern_info(regex, PCRE2_INFO_NAMETABLE, &name_table);

	static_assert(PCRE2_CODE_UNIT_WIDTH == 8, "Need to fix name table parsing for non-8bit pcre2");
	for (i = 0; i < name_table_count; i++)
		named_capture_groups.insert(std::string((char*)(name_table + name_table_entry_size*i + 2)));

	return true;
}

bool ShaderRegexPattern::named_group_overlaps(ShaderRegexTemps &other_set)
{
	ShaderRegexTemps intersection;

	// C++ why you be so verbose?
	std::set_intersection(
				named_capture_groups.begin(),
				named_capture_groups.end(),
				other_set.begin(),
				other_set.end(),
				std::inserter(intersection, intersection.begin()));

	return intersection.size() != 0;
}

bool ShaderRegexPattern::matches(std::string *asm_text)
{
	pcre2_match_data *match_data = NULL;
	bool match = false;
	int rc;

	// TODO: Assign per-thread JIT stack if the default 32K turns out to be
	// insufficient. Can probably store this in the context, as that is
	// supposed to be per-thread, or use thread local storage.

	match_data = pcre2_match_data_create_from_pattern(regex, NULL);

	// TODO: Consider using pcre2_jit_match - doco claims 10% faster, but
	// has less sanity checks. TODO: Use callback to confirm JIT was used
	rc = pcre2_match(regex, (PCRE2_SPTR)asm_text->c_str(), asm_text->length(), 0, 0, match_data, NULL);
	if (rc == PCRE2_ERROR_NOMATCH)
		goto out_free;
	if (rc < 0) {
		log_pcre2_error_nonl(rc, "  WARNING: regex match error");
		goto out_free;
	}

	match = true;

out_free:
	pcre2_match_data_free(match_data);
	return match;
}

static void replacement_search_and_replace(std::string &str, std::string *search, std::string *replace)
{
	size_t pos;

	for (pos = str.find(*search); pos != std::string::npos; pos = str.find(*search, pos + 1)) {
		if (pos > 0 && (str[pos-1] == '$' || str[pos-1] == '\\'))
			continue;

		str.replace(pos, search->length(), *replace);
	}
}

static void substitute_temp_regs(std::string &replacement, ShaderRegexTemps *temp_regs, unsigned dcl_temps)
{
	ShaderRegexTemps::iterator i;
	unsigned tmp_reg = dcl_temps;
	std::string search_str, repl_str;

	for (i = temp_regs->begin(); i != temp_regs->end(); i++, tmp_reg++) {
		repl_str = std::string("r") + std::to_string(tmp_reg);

		search_str = std::string("$") + *i;
		replacement_search_and_replace(replacement, &search_str, &repl_str);

		search_str = std::string("${") + *i + std::string("}");
		replacement_search_and_replace(replacement, &search_str, &repl_str);
	}
}

bool ShaderRegexPattern::patch(std::string *asm_text, ShaderRegexTemps *temp_regs, unsigned dcl_temps)
{
	pcre2_match_data *match_data = NULL;
	PCRE2_SIZE est_size, output_size;
	std::string replace_copy;
	PCRE2_UCHAR *buf = NULL;
	bool patch = false;
	uint32_t options;
	int rc;

	if (!temp_regs->empty() && (dcl_temps > D3D11_COMMONSHADER_TEMP_REGISTER_COUNT ||
		temp_regs->size() > D3D11_COMMONSHADER_TEMP_REGISTER_COUNT - dcl_temps)) {
		LogInfo("WARNING: ShaderRegex temporary-register allocation exceeds the shader limit\n");
		return false;
	}

	static_assert(PCRE2_CODE_UNIT_WIDTH == 8, "Need to fix output buffer allocation for non-8bit pcre2");

	// We operate on a copy of the replace string so that future shaders
	// don't get our temporary register numbers:
	replace_copy = replace;
	substitute_temp_regs(replace_copy, temp_regs, dcl_temps);

	// TODO: Allow named capture groups from other patterns in the same
	// regex group to be substituted in, and provide some simple arithmetic
	// operators to e.g. allow a constant buffer byte offset to be divided
	// by 16 to get the constant buffer index and vice versa

	// At a minimum we want \n to be translated in the replace string,
	// which needs extended substitution processing to be enabled:
	options = PCRE2_SUBSTITUTE_EXTENDED;

	match_data = pcre2_match_data_create_from_pattern(regex, NULL);

	output_size = est_size = asm_text->length() + replace_copy.length() + 1024;
	buf = new PCRE2_UCHAR[output_size];
	rc = pcre2_substitute(regex,
			(PCRE2_SPTR)asm_text->c_str(), asm_text->length(), 0,
			options | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH,
			match_data, NULL,
			(PCRE2_SPTR)replace_copy.c_str(), replace_copy.length(),
			buf, &output_size);

	if (rc == PCRE2_ERROR_NOMEMORY) {
		LogInfo("  NOTICE: regex replace requires a %u byte buffer\n", (unsigned)output_size);
		LogInfo("  NOTICE: We underestimated by %u bytes and have to start over\n", (unsigned)(output_size - est_size));
		LogInfo("  NOTICE: What kind of crazy are you doing to get down this code path?\n");
		LogInfo("  NOTICE: You didn't inject a matrix inverse or two in assembly did you?\n");
		LogInfo("  NOTICE: Once more, with passion!\n");

		delete [] buf;
		buf = new PCRE2_UCHAR[output_size];

		rc = pcre2_substitute(regex,
				(PCRE2_SPTR)asm_text->c_str(), asm_text->length(), 0,
				options, // No PCRE2_SUBSTITUTE_OVERFLOW_LENGTH this time
				match_data, NULL,
				(PCRE2_SPTR)replace_copy.c_str(), replace_copy.length(),
				buf, &output_size);
	}

	if (rc == 0)
		goto out_free;
	if (rc < 0) {
		log_pcre2_error_nonl(rc, "  WARNING: regex replace error");
		goto out_free;
	}

	*asm_text = (char*)buf;
	patch = true;

out_free:
	pcre2_match_data_free(match_data);
	delete [] buf;

	return patch;
}

void ShaderRegexGroup::apply_regex_patterns(std::string *asm_text, bool *match, bool *patch)
{
	ShaderRegexPatterns::iterator i;
	ShaderRegexPattern *pattern;
	unsigned dcl_temps = 0;

	// Match defaults to true so that if there are no patterns we can still
	// apply the command list. Patch defaults to false because we don't
	// want to waste time re-assembling the shader if we didn't change it.
	*match = true;
	*patch = false;

	if (!temp_regs.empty() && !get_dcl_temps(asm_text, &dcl_temps)) {
		*match = false;
		return;
	}

	for (i = patterns.begin(); i != patterns.end(); i++) {
		pattern = &i->second;

		if (pattern->do_replace)
			*match = *patch = pattern->patch(asm_text, &temp_regs, dcl_temps);
		else
			*match = pattern->matches(asm_text);

		if (!*match) {
			*patch = false;
			return;
		}
	}

	// Only update dcl_temps if we are patching:
	if (*patch && !temp_regs.empty())
		*patch = update_dcl_temps(asm_text, dcl_temps + temp_regs.size());

	// But we can update declarations even if we aren't doing a regex
	// replace in some cases, so long as the patterns all matched (e.g.
	// globally disable the driver stereo cb):
	if (!declarations.empty())
		*patch = insert_declarations(asm_text, &declarations) || *patch;
}

bool ShaderRegexGroup::add_excluded_hash(const std::wstring& value)
{
    size_t start = value.size() >= 2 && value[0] == L'0' &&
        (value[1] == L'x' || value[1] == L'X') ? 2 : 0;
    if (value.size() == start || value.size() - start > 16)
        return false;
    UINT64 hash = 0;
    for (size_t i = start; i < value.size(); ++i) {
        const wchar_t c = value[i];
        unsigned digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else return false;
        hash = (hash << 4) | digit;
    }
    excluded_hashes.insert(hash);
    return true;
}

void ShaderRegexGroup::link_command_lists_and_filter_index(UINT64 shader_hash)
{
	if (excludes(shader_hash))
		return;
	ShaderOverride *shader_override = NULL;
	wstring ini_section, ini_line;
	CommandList::Commands::reverse_iterator i;

	// Only link the command lists if we have something in ours to link in,
	// because this will create ShaderOverride sections for shaders that
	// don't already have one, adding more work in the draw calls.

	if (command_list.noop() && post_command_list.noop() && filter_index == FLT_MAX)
		return;

	const auto result = G->mShaderOverrideMap.try_emplace(shader_hash);
	shader_override = &result.first->second;
	if (result.second)
		G->shader_override_generation.fetch_add(1, std::memory_order_release);

	// Initialise the ShaderOverride's command lists if they aren't already:
	if (shader_override->command_list.ini_section.empty()) {
		ini_section = command_list.ini_section + L".Match";
		shader_override->command_list.ini_section = ini_section;
		shader_override->post_command_list.ini_section = ini_section;
		shader_override->post_command_list.post = true;
	}

	// Set the filter index for partner filtering:
	if (shader_override->filter_index == FLT_MAX)
		shader_override->filter_index = filter_index;

	// If we have previously linked a command list (on any matched shader)
	// we will reuse the link command here, after checking that this
	// matched shader has not already been linked. Avoids the command lists
	// growing endlessly and eventually killing performance.
	if (link) {
		for (i = shader_override->command_list.commands.rbegin();
		         i != shader_override->command_list.commands.rend(); i++) {
			if (*i == link)
				return;
		}
		shader_override->command_list.DiscardLiteralBatch();
		shader_override->command_list.commands.push_back(link);
		if (post_link) {
			shader_override->post_command_list.DiscardLiteralBatch();
			shader_override->post_command_list.commands.push_back(post_link);
		}
		return;
	} else if (post_link) {
		for (i = shader_override->post_command_list.commands.rbegin();
		         i != shader_override->post_command_list.commands.rend(); i++) {
			if (*i == post_link)
				return;
		}
		shader_override->post_command_list.DiscardLiteralBatch();
		shader_override->post_command_list.commands.push_back(post_link);
		return;
	}

	// This is the first shader this pattern has matched. Create a new
	// RunLinkedCommandList command and link it up:
	ini_line = L"[" + command_list.ini_section + L".Match] run = linked command list";

	if (!command_list.noop())
		link = LinkCommandLists(&shader_override->command_list, &command_list, &ini_line);

	if (!post_command_list.noop())
		post_link = LinkCommandLists(&shader_override->post_command_list, &post_command_list, &ini_line);
}

bool unlink_shader_regex_command_lists_and_filter_index(UINT64 shader_hash)
{
	ShaderOverride *shader_override = NULL;
	CommandList::Commands::iterator i, next;
	RunLinkedCommandList *link;
	bool ret = false;

	auto shader_override_i = G->mShaderOverrideMap.find(shader_hash);
	if (shader_override_i == G->mShaderOverrideMap.end())
		return false;

	shader_override = &shader_override_i->second;

	for (i = shader_override->command_list.commands.begin(), next = i;
	    i != shader_override->command_list.commands.end(); i = next) {
		next++;
		link = dynamic_cast<RunLinkedCommandList*>(i->get());
		if (link) {
			shader_override->command_list.DiscardLiteralBatch();
			next = shader_override->command_list.commands.erase(i);
			ret = true;
		}
	}

	for (i = shader_override->post_command_list.commands.begin(), next = i;
	    i != shader_override->post_command_list.commands.end(); i = next) {
		next++;
		link = dynamic_cast<RunLinkedCommandList*>(i->get());
		if (link) {
			shader_override->post_command_list.DiscardLiteralBatch();
			next = shader_override->post_command_list.commands.erase(i);
			ret = true;
		}
	}

	if (shader_override->filter_index != shader_override->backup_filter_index) {
		shader_override->filter_index = shader_override->backup_filter_index;
		ret = true;
	}

	return ret;
}

#define SHADER_REGEX_CACHE_VERSION 2
struct ShaderRegexCacheHeader {
	uint32_t version;
	uint32_t shader_regex_hash;
	uint32_t patched;
	uint32_t num_matches;
};

static bool shader_regex_cache_path_prefix_ok(const wchar_t *path, int n, const char *operation)
{
	if (n < 0) {
		const size_t path_len = wcslen(path);
		const wchar_t *tail = path_len > 16 ? path + path_len - 16 : path;
		LogInfo("ShaderRegexCache: %s rejected truncated cache path (path_len=%Iu, tail='...%S')\n",
			operation, path_len, tail);
		return false;
	}
	if (n > MAX_PATH - 4) {
		LogInfo("ShaderRegexCache: %s rejected cache path without suffix space (n=%d, max_prefix=%d)\n",
			operation, n, MAX_PATH - 4);
		return false;
	}
	return true;
}

ShaderRegexCache load_shader_regex_cache(UINT64 hash, const wchar_t *shader_type, vector<byte> *bytecode, std::wstring *tagline)
{
	ShaderRegexCache ret = ShaderRegexCache::NO_CACHE;
	HANDLE meta_f = INVALID_HANDLE_VALUE;
	HANDLE bin_f = INVALID_HANDLE_VALUE;
	ShaderRegexCacheHeader *header;
	ShaderRegexGroup *group;
	wchar_t path[MAX_PATH];
	uint32_t *match_ids;
	DWORD size, size2;
	byte *buf = NULL;
	size_t suffix;
	int n;
	uint32_t i;

	n = _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls\\%016llx-%ls_regex.", G->SHADER_CACHE_PATH, hash, shader_type);
	if (!shader_regex_cache_path_prefix_ok(path, n, "load"))
		return ret;
	suffix = (size_t)n;
	wcscpy_s(path+suffix, MAX_PATH-suffix, L"dat");
	meta_f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (meta_f == INVALID_HANDLE_VALUE)
		return ret;

	size = GetFileSize(meta_f, 0);
	if (size < sizeof(ShaderRegexCacheHeader))
		goto out;

	buf = new byte[size];

	if (!ReadFile(meta_f, buf, size, &size2, NULL) || size != size2)
		goto out;

	header = (ShaderRegexCacheHeader*)buf;
	match_ids = (uint32_t*)(buf + sizeof(ShaderRegexCacheHeader));

	if (header->version != SHADER_REGEX_CACHE_VERSION
	 || header->shader_regex_hash != shader_regex_hash)
		goto out;

	if (size != sizeof(ShaderRegexCacheHeader) + header->num_matches * sizeof(uint32_t))
		goto out;

	// num_matches may be 0, which means the ShaderRegex didn't match the
	// shader, but we cache it anyway to skip processing the shader again.
	// We don't really need any special handling for this case, since
	// returning MATCH will already skip that handling in the caller, but
	// we return a special value so the caller can log it appropriately.
	if (header->num_matches == 0) {
		ret = ShaderRegexCache::NO_MATCH;
		goto out;
	}

	for (i = 0; i < header->num_matches; ++i) {
        if (match_ids[i] >= shader_regex_group_index.size() ||
            shader_regex_group_index[match_ids[i]]->excludes(hash))
            goto out;
    }

	for (i = 0; i < header->num_matches; i++) {
		// The ShaderRegex groups are sorted and since the cached hash
		// already matched the map should be identical to when the
		// cache was made, so we can use that to find the matching
		// groups without having to do an expensive lookup by name:
		if (match_ids[i] >= shader_regex_group_index.size())
			goto out;
		group = shader_regex_group_index[match_ids[i]];

		LogInfo("ShaderRegexCache: %S %016I64x matches [%S]\n", shader_type, hash, group->ini_section.c_str());

		if (header->patched && tagline)
			tagline->append(std::wstring(L"[") + group->ini_section + std::wstring(L"]"));

		group->link_command_lists_and_filter_index(hash);
	}

	if (header->patched) {
		wcscpy_s(path+suffix, MAX_PATH-suffix, L"bin");
		bin_f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (bin_f == INVALID_HANDLE_VALUE)
			goto out;
		size = GetFileSize(bin_f, 0);
		bytecode->resize(size);
		if (!size || !ReadFile(bin_f, bytecode->data(), size, &size2, NULL) || size != size2)
			goto out;
		ret = ShaderRegexCache::PATCH;
	} else
		ret = ShaderRegexCache::MATCH;

out:
	if (buf)
		delete [] buf;
	if (bin_f != INVALID_HANDLE_VALUE)
		CloseHandle(bin_f);
	if (meta_f != INVALID_HANDLE_VALUE)
		CloseHandle(meta_f);
	return ret;
}

static void save_shader_regex_cache_meta(UINT64 hash, const wchar_t *shader_type, vector<uint32_t> *match_ids,
		bool patched, std::string *asm_text, std::wstring *tagline)
{
	ShaderRegexCacheHeader header;
	wchar_t path[MAX_PATH];
	FILE *f = NULL;
	size_t suffix;
	int n;

	if (!G->SHADER_CACHE_PATH[0] || (!G->CACHE_SHADERS && !G->EXPORT_FIXED))
		return;

	n = _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls\\%016llx-%ls_regex.", G->SHADER_CACHE_PATH, hash, shader_type);
	if (!shader_regex_cache_path_prefix_ok(path, n, "save"))
		return;
	suffix = (size_t)n;

	if (G->CACHE_SHADERS) {
		// TODO: When we have a condition field in ShaderRegex: The evaluations
		// of *all* valid conditions (not just those matched) must qualify the
		// cache, either by encoding them in the filename or extending the
		// metadata format.

		// Make sure there isn't an old stale .bin file *before* writing the
		// new metadata to make sure it can't be loaded by mistake. If we can't
		// remove it (e.g. another thread is currently reading it or permission
		// issues) it's better not to update the cache at all:
		wcscpy_s(path+suffix, MAX_PATH-suffix, L"bin");
		if (!DeleteFile(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
			return;

		wcscpy_s(path+suffix, MAX_PATH-suffix, L"dat");
		wfopen_ensuring_access(&f, path, L"wb");
		if (!f)
			return;

		header.version = SHADER_REGEX_CACHE_VERSION;
		header.shader_regex_hash = shader_regex_hash;
		header.patched = patched;
		header.num_matches = (uint32_t)match_ids->size();
		fwrite(&header, 1, sizeof(ShaderRegexCacheHeader), f);
		fwrite(match_ids->data(), sizeof(uint32_t), match_ids->size(), f);

		fclose(f);
	}

	if (G->EXPORT_FIXED) {
		wcscpy_s(path+suffix, MAX_PATH-suffix, L"txt");
		if (patched) {
			wfopen_ensuring_access(&f, path, L"wb");
			if (!f) {
				LogInfo("  Error storing ShaderRegex assembly to %S\n", path);
				return;
			}

			fprintf_s(f, "%S\n", tagline->c_str());
			fwrite(asm_text->c_str(), 1, asm_text->size(), f);

			fclose(f);
			LogInfo("  Storing ShaderRegex assembly to %S\n", path);
		} else {
			if (DeleteFile(path))
				LogInfo("  Removed stale ShaderRegex assembly file %S\n", path);
		}
	}
}

void save_shader_regex_cache_bin(UINT64 hash, const wchar_t *shader_type, vector<byte> *bytecode)
{
	wchar_t path[MAX_PATH];
	FILE *f = NULL;
	int n;

	if (!G->CACHE_SHADERS || !G->SHADER_CACHE_PATH[0])
		return;

	n = _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls\\%016llx-%ls_regex.bin", G->SHADER_CACHE_PATH, hash, shader_type);
	if (n < 0) {
		LogInfo("ShaderRegexCache: save bin rejected truncated cache path (path_len=%Iu)\n", wcslen(path));
		return;
	}

	wfopen_ensuring_access(&f, path, L"wb");
	if (!f)
		return;
	fwrite(bytecode->data(), 1, bytecode->size(), f);
	fclose(f);
}

bool get_shader_model_from_bytecode(const void* data, size_t size, std::string* out_model)
{
	if (!data || size < 32 || !out_model)
		return false;

	const uint8_t* buffer = static_cast<const uint8_t*>(data);

	// Validate DXBC header
	if (memcmp(buffer, "DXBC", 4) != 0)
		return false;

	uint32_t numChunks;
	memcpy(&numChunks, buffer + 28, sizeof(numChunks));

	// Check the table numerically before constructing pointers into it.
	if (numChunks == 0 || numChunks > (size - 32) / sizeof(uint32_t))
		return false;

	// Iterate chunks backwards (same as disassembler)
	for (size_t i = numChunks; i-- > 0;)
	{
		uint32_t chunkOffset;
		memcpy(&chunkOffset, buffer + 32 + i * sizeof(chunkOffset), sizeof(chunkOffset));
		const size_t offset = chunkOffset;

		if (offset > size || size - offset < 12)
			continue;

		const uint8_t* chunk = buffer + offset;

		// Look for shader code chunk
		if (memcmp(chunk, "SHEX", 4) != 0 && memcmp(chunk, "SHDR", 4) != 0)
			continue;

		// Version token is at +8
		uint32_t versionToken;
		memcpy(&versionToken, chunk + 8, 4);

		uint32_t type = (versionToken >> 16) & 0xFFFF;
		uint32_t major = (versionToken >> 4) & 0xF;
		uint32_t minor = (versionToken >> 0) & 0xF;

		const char* prefix = "xx";
		switch (type)
		{
			case 0: prefix = "ps"; break;
			case 1: prefix = "vs"; break;
			case 2: prefix = "gs"; break;
			case 3: prefix = "hs"; break;
			case 4: prefix = "ds"; break;
			case 5: prefix = "cs"; break;
		}

		char buf[16];
		snprintf(buf, sizeof(buf), "%s_%u_%u", prefix, major, minor);

		*out_model = buf;
		return true;
	}

	return false;
}

// Process groups that do not have patches to apply. Those can be handled without disassembly.
void link_shader_regex_groups_without_patterns(const wchar_t* shader_type, std::string* shader_model, UINT64 hash, bool* decompilation_required)
{
	ShaderRegexGroups::iterator i;
	vector<uint32_t> match_ids;
	vector<ShaderRegexGroup*> match_groups;
	uint32_t j;

	for (i = shader_regex_groups.begin(), j = 0; i != shader_regex_groups.end(); i++, j++) {
		ShaderRegexGroup* group = &i->second;

		if (group->excludes(hash))
			continue;

		// Skip group without matching shader model.
		if (!group->shader_models.count(*shader_model)) {
			continue;
		}

		// Skip group with patterns. Those ones need txt to match.
		if (!group->patterns.empty()) {
			if (decompilation_required)
				*decompilation_required = true;
			continue;
		}

		match_ids.push_back(j);
		match_groups.push_back(group);

		LogInfo("ShaderRegex (no pattern): %S %016I64x matches [%S]\n", shader_type, hash, group->ini_section.c_str());
	}

	// If no ShaderRegEx requires decompilation, link CommandLists and update shader cache here instead of `apply_shader_regex_groups`. 
	if (decompilation_required && !*decompilation_required) {
		// Enable CommandList sections execution for this group.
		for (ShaderRegexGroup* group : match_groups) {
			group->link_command_lists_and_filter_index(hash);
		}
		// We save the cache metadata even if we didn't match anything. That
		// way we can skip checking for a match next time when we know there
		// won't be any. This only saves the metadata - the caller will use
		// save_shader_regex_cache_bin to save the assembled binary.
		save_shader_regex_cache_meta(hash, shader_type, &match_ids, false, nullptr, nullptr);
	}
}

bool apply_shader_regex_groups(std::string *asm_text, const wchar_t *shader_type, std::string *shader_model, UINT64 hash, std::wstring *tagline)
{
	ShaderRegexGroups::iterator i;
	ShaderRegexGroup *group;
	bool patched = false;
	bool match, patch;
	vector<uint32_t> match_ids;
	uint32_t j;

	for (i = shader_regex_groups.begin(), j = 0; i != shader_regex_groups.end(); i++, j++) {
		group = &i->second;

		if (group->excludes(hash))
			continue;

		// Skip group without matching shader model.
		if (!group->shader_models.count(*shader_model)) {
			continue;
		}

		// Match/patch only ShaderRegEx with Pattern.
		if (!group->patterns.empty()) {
			// Run patch.
			group->apply_regex_patterns(asm_text, &match, &patch);
			if (!match)
				continue;

			LogInfo("ShaderRegex: %s %016I64x matches [%S]\n", shader_model->c_str(), hash, group->ini_section.c_str());
			patched = patched || patch;

			// Append section to patch sequence.
			if (patch && tagline)
				tagline->append(std::wstring(L"[") + group->ini_section + std::wstring(L"]"));
		}

		match_ids.push_back(j);

		// Enable CommandList sections execution for this group.
		group->link_command_lists_and_filter_index(hash);
	}

	// We save the cache metadata even if we didn't match anything. That
	// way we can skip checking for a match next time when we know there
	// won't be any. This only saves the metadata - the caller will use
	// save_shader_regex_cache_bin to save the assembled binary.
	save_shader_regex_cache_meta(hash, shader_type, &match_ids, patched, asm_text, tagline);

	return patched;
}
