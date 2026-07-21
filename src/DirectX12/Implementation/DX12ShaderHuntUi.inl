static void FormatStageItem(
	DX12HuntListItem *item, size_t displayIndex, UINT64 hash, const StageSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %016llx", displayIndex,
		static_cast<unsigned long long>(hash));
	item->hash = hash;
	item->selected = selection.armed && SelectedHashLocked(selection) == hash;
}

static void FormatIaItem(
	DX12HuntListItem *item, size_t displayIndex, uint32_t hash, const IaSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %08x", displayIndex, hash);
	item->hash = hash;
	item->selected = selection.armed && SelectedIaHashLocked(selection) == hash;
}

size_t DX12HuntGetListItems(
	DX12HuntListCategory category, DX12HuntListItem *items, size_t itemCount)
{
	size_t total = 0;
	AcquireSRWLockShared(&gHuntLock);
	switch (category) {
	case DX12_HUNT_LIST_VS:
		total = gVS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gVS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gVS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_PS:
		total = gPS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gPS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gPS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_CS:
		total = gCS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gCS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gCS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_IB:
		total = gIB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gIB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gIB);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_VB:
		total = gVB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gVB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gVB);
				++index;
			}
		}
		break;
	default:
		break;
	}
	ReleaseSRWLockShared(&gHuntLock);
	return total;
}

template <typename Selection>
static bool SelectByIndexLocked(Selection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	auto it = selection.visited.begin();
	std::advance(it, index);
	selection.selectedHash = *it;
	return true;
}

template <>
bool SelectByIndexLocked<StageSelection>(StageSelection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	return true;
}

void DX12HuntSelectListItem(DX12HuntListCategory category, size_t index)
{
	if (!DX12HuntIsEnabled())
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			selected = SelectByIndexLocked(gVS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_PS:
			selected = SelectByIndexLocked(gPS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::PS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_CS:
			selected = SelectByIndexLocked(gCS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::CS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_IB:
			selected = SelectByIndexLocked(gIB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::IB : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_VB:
			selected = SelectByIndexLocked(gVB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VB : gLastCopyTarget;
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}

void DX12HuntSelectListHash(DX12HuntListCategory category, UINT64 hash)
{
	if (!DX12HuntIsEnabled() || !hash)
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			if (gVS.visited.find(hash) != gVS.visited.end()) {
				gVS.armed = true;
				gVS.selected = static_cast<size_t>(std::distance(gVS.visited.begin(), gVS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::VS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_PS:
			if (gPS.visited.find(hash) != gPS.visited.end()) {
				gPS.armed = true;
				gPS.selected = static_cast<size_t>(std::distance(gPS.visited.begin(), gPS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::PS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_CS:
			if (gCS.visited.find(hash) != gCS.visited.end()) {
				gCS.armed = true;
				gCS.selected = static_cast<size_t>(std::distance(gCS.visited.begin(), gCS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::CS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_IB:
			if (hash <= UINT32_MAX && gIB.visited.find(static_cast<uint32_t>(hash)) != gIB.visited.end()) {
				gIB.armed = true;
				gIB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gIB);
				gLastCopyTarget = HuntCopyTarget::IB;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_VB:
			if (hash <= UINT32_MAX && gVB.visited.find(static_cast<uint32_t>(hash)) != gVB.visited.end()) {
				gVB.armed = true;
				gVB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gVB);
				gLastCopyTarget = HuntCopyTarget::VB;
				selected = true;
			}
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}
