static bool CommandListRuntimeEffectLocked(const std::wstring &name)
{
	auto it = gCommandListRuntimeEffect.find(name);
	return it == gCommandListRuntimeEffect.end() || it->second;
}

static bool CommandListRuntimeEffect(const std::wstring &name)
{
	AcquireSRWLockShared(&gModLock);
	const bool effect = CommandListRuntimeEffectLocked(name);
	ReleaseSRWLockShared(&gModLock);
	return effect;
}

static bool SnapshotCommandListConfig(
	const std::wstring &name, Bunny::CommandListConfig *config)
{
	if (!config)
		return false;
	AcquireSRWLockShared(&gModLock);
	auto it = gCommandLists.find(name);
	if (it == gCommandLists.end()) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	*config = it->second;
	ReleaseSRWLockShared(&gModLock);
	return true;
}

static bool SnapshotCompiledTextureOverridePlan(
	const Bunny::TextureOverrideConfig &config,
	DX12CompiledTextureOverridePlan *plan)
{
	if (!plan)
		return false;
	AcquireSRWLockShared(&gModLock);
	auto sectionIdIt = gTextureOverrideSectionIds.find(config.section);
	if (sectionIdIt == gTextureOverrideSectionIds.end() || !sectionIdIt->second) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	auto planIt = gCompiledTextureOverridePlans.find(sectionIdIt->second);
	if (planIt == gCompiledTextureOverridePlans.end()) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	*plan = planIt->second;
	ReleaseSRWLockShared(&gModLock);
	return true;
}

static bool SnapshotCompiledCommandListPlan(
	const std::wstring &name,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return false;
	AcquireSRWLockShared(&gModLock);
	auto planIt = gCompiledCommandListPlans.find(name);
	if (planIt == gCompiledCommandListPlans.end() || planIt->second.unsupported) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	*plan = planIt->second;
	ReleaseSRWLockShared(&gModLock);
	return true;
}

static DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}

class DX12CommandListExecutor
{
public:
	class CommandVmRunScope
	{
	public:
		MigotoCommandVmUtils::State *state = nullptr;
		size_t branchDepth = 0;
		bool flushPost = false;

		CommandVmRunScope(MigotoCommandVmUtils::State *vmState, bool flushPostAssignments)
			: state(vmState),
			  flushPost(flushPostAssignments)
		{
			if (!state)
				return;
			branchDepth = state->branches.size();
			state->PushLocalScope();
		}

		~CommandVmRunScope()
		{
			if (!state)
				return;
			if (state->branches.size() > branchDepth)
				state->branches.resize(branchDepth);
			if (flushPost)
				state->FlushPostAssignments();
			state->PopLocalScope();
		}
	};

	DX12CommandListExecutor(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12IaHashState &iaState,
		uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
		DX12ModIaReplacement *replacement,
		std::vector<DX12PendingResourceViewRequest> *pendingResourceViews = nullptr)
		: mDevice(device),
		  mCommandList(commandList),
		  mIaState(iaState),
		  mVertexCount(vertexCount),
		  mIndexCount(indexCount),
		  mInstanceCount(instanceCount),
		  mFirstVertex(firstVertex),
		  mFirstIndex(firstIndex),
		  mFirstInstance(firstInstance),
		  mReplacement(replacement),
		  mPendingResourceViews(pendingResourceViews)
	{
	}

	void SetExecutionMode(bool executeCommands, bool executeDrawActions)
	{
		mExecuteCommands = executeCommands;
		mExecuteDrawActions = executeDrawActions;
	}

	bool Changed() const
	{
		return mChanged;
	}

	void ClearChanged()
	{
		mChanged = false;
	}

	void MarkExternalChange()
	{
		MarkChanged();
	}

	void RunTextureOverride(
		const Bunny::TextureOverrideConfig &config, bool includePost)
	{
		DX12TextureOverrideMatch match;
		match.config = config;
		RunTextureOverride(match, 0, includePost);
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, bool includePost)
	{
		RunCommandListLinks(links, 0, includePost, nullptr);
	}

	void RunShaderOverride(const Bunny::ShaderOverrideConfig &config)
	{
		if (!mReplacement)
			return;
		if (config.handlingSkip)
			SetSkip();
		RunCommandListLinks(config.commandLists, 0, false, nullptr);
		RunActions(config.actions, 0, false, nullptr);
	}

	void RunPostTextureOverrideLists(
		const Bunny::TextureOverrideConfig &config, bool postRuntimeEffect)
	{
		if (!postRuntimeEffect)
			return;
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
	}

	void RunPostShaderOverrideLists(
		const Bunny::ShaderOverrideConfig &config)
	{
		Bunny::CommandListLinks postLinks;
		postLinks.main = config.commandLists.post;
		RunCommandListLinks(postLinks, 0, true, nullptr);
	}

public:
	void MarkChanged()
	{
		mChanged = true;
	}

	void SetSkip()
	{
		if (mReplacement)
			mReplacement->skip = true;
	}

	void AppendDraw(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed =
			action.kind == Bunny::CommandListActionKind::DrawIndexed ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.fromCaller =
			action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		if (!draw.fromCaller) {
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		mReplacement->draws.push_back(draw);
		MarkChanged();
	}

	void AppendDispatch(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		mReplacement->dispatches.push_back(dispatch);
		MarkChanged();
	}

	void AppendResourceViews(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (mPendingResourceViews) {
			DX12PendingResourceViewRequest request;
			request.indexResource = indexResource;
			request.vertexResources = vertexResources;
			mPendingResourceViews->push_back(std::move(request));
			return;
		}
		mChanged |= AppendResourceViewsForCommandList(
			mDevice, mCommandList, mIaState, indexResource, vertexResources, mReplacement);
	}

	bool FindTargetTextureOverride(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches) const
	{
		return ::FindTargetTextureOverride(
			mCommandList, mIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex, mFirstInstance, matches);
	}

	void RunTextureOverride(
		const DX12TextureOverrideMatch &match, int depth, bool includePost)
	{
		const Bunny::TextureOverrideConfig &config = match.config;
		if (!mReplacement || depth > 16)
			return;

		if (mExecuteCommands) {
			for (const std::wstring &list : config.commandLists.pre) {
				Bunny::CommandListLinks links;
				links.main.push_back(list);
				RunCommandListLinks(links, depth + 1, false, &match);
			}
		}

		if (!RunCompiledTextureOverridePlan(config)) {
			RunTextureOverrideActions(config, depth, includePost, &match);
		}

		if (mExecuteCommands) {
			for (const std::wstring &list : config.commandLists.main) {
				Bunny::CommandListLinks links;
				links.main.push_back(list);
				RunCommandListLinks(links, depth + 1, false, &match);
			}

			if (includePost) {
				for (const std::wstring &list : config.commandLists.post) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, true, &match);
				}
			}
		}

		if (DX12DrawSrvReplacementUtils::ApplyConfig(
			    mDevice, mCommandList, config, &match, mReplacement))
			MarkChanged();
	}

	bool RunCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
	{
		DX12CompiledTextureOverridePlan plan;
		if (!SnapshotCompiledTextureOverridePlan(config, &plan))
			return false;
		if (plan.hasUnsupportedCommandActions)
			return false;

		AppendResourceViews(plan.indexResource, plan.vertexResources);
		const bool executeActions = mExecuteCommands || mExecuteDrawActions;
		if (executeActions && plan.handlingSkip)
			SetSkip();
		if (executeActions) {
			for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
				mReplacement->draws.push_back(draw);
				MarkChanged();
			}
			for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
				mReplacement->dispatches.push_back(dispatch);
				MarkChanged();
			}
		}
		return true;
	}

	bool RunCompiledCommandListPlan(const std::wstring &name)
	{
		if (!mReplacement)
			return false;
		if (!EnterCommandList(name))
			return true;
		DX12CompiledCommandListPlan plan;
		if (!SnapshotCompiledCommandListPlan(name, &plan)) {
			LeaveCommandList();
			return false;
		}
		ExecuteCompiledCommandListPlan(plan);
		LeaveCommandList();
		return true;
	}

	void ExecuteCompiledCommandListPlan(const DX12CompiledCommandListPlan &plan)
	{
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		if (plan.handlingSkip)
			SetSkip();
		for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
			mReplacement->draws.push_back(draw);
			MarkChanged();
		}
		for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
			mReplacement->dispatches.push_back(dispatch);
			MarkChanged();
		}
	}

	void RunTextureOverrideActions(
		const Bunny::TextureOverrideConfig &config, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		RunActions(config.actions, depth, includePost, thisMatch);
	}

	void RunCommandList(
		const Bunny::CommandListConfig &commandList, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;
		if (RunCompiledCommandListPlan(commandList.section))
			return;
		if (!EnterCommandList(commandList.section))
			return;
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : commandList.actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				Bunny::CommandListConfig config;
				if (SnapshotCommandListConfig(action.commandList, &config))
					RunCommandList(config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement)
					break;
				std::vector<DX12TextureOverrideMatch> matches;
				if (FindTargetTextureOverride(action.target, &matches)) {
					for (const DX12TextureOverrideMatch &match : matches)
						RunTextureOverride(match, depth + 1, includePost);
				}
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (mReplacement)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (mReplacement)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (mReplacement)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetConstantBuffer:
			case Bunny::CommandListActionKind::SetShaderResource:
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
		LeaveCommandList();
	}

	void RunActions(
		const std::vector<Bunny::CommandListAction> &actions, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		CommandVmRunScope vmScope(&mVm, includePost);
		for (const Bunny::CommandListAction &action : actions) {
			if (ApplyVmAction(action, includePost))
				continue;
			if (!mVm.Active())
				continue;
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				if (!mExecuteCommands)
					break;
				Bunny::CommandListConfig config;
				if (SnapshotCommandListConfig(action.commandList, &config))
					RunCommandList(config, depth + 1, includePost, thisMatch);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mReplacement || !mExecuteCommands)
					break;
				std::vector<DX12TextureOverrideMatch> matches;
				if (FindTargetTextureOverride(action.target, &matches)) {
					for (const DX12TextureOverrideMatch &match : matches)
						RunTextureOverride(match, depth + 1, includePost);
				}
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (executeActions)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				if (mReplacement)
					AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				if (mReplacement)
					AppendResourceViews(
						L"", std::vector<DX12CompiledVertexResourceBinding>{
							{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (executeActions)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (executeActions)
					AppendDispatch(action);
				break;
			case Bunny::CommandListActionKind::ControlFlow:
			case Bunny::CommandListActionKind::VariableAssignment:
			case Bunny::CommandListActionKind::PostVariableAssignment:
			case Bunny::CommandListActionKind::LocalVariableAssignment:
			case Bunny::CommandListActionKind::ResourceAssignment:
			case Bunny::CommandListActionKind::SetConstantBuffer:
			case Bunny::CommandListActionKind::SetShaderResource:
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
			case Bunny::CommandListActionKind::SetShader:
			case Bunny::CommandListActionKind::SetShaderConstant:
			case Bunny::CommandListActionKind::SetRenderState:
				break;
			}
		}
	}

	bool ApplyVmAction(const Bunny::CommandListAction &action, bool includePost)
	{
		switch (action.kind) {
		case Bunny::CommandListActionKind::ControlFlow:
			mVm.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return true;
		case Bunny::CommandListActionKind::VariableAssignment:
			if (mVm.Active())
				mVm.Assign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::PostVariableAssignment:
			if (includePost && mVm.Active())
				mVm.QueuePostAssign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			if (mVm.Active())
				mVm.DeclareLocal(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::SetShaderConstant:
			if (mVm.Active())
				mVm.AssignSymbol(action.key, action.value);
			return true;
		default:
			return false;
		}
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, int depth, bool includePost,
		const DX12TextureOverrideMatch *thisMatch)
	{
		if (depth > 16)
			return;

		for (const std::wstring &list : links.pre) {
			if (!CommandListRuntimeEffect(list))
				continue;
			if (RunCompiledCommandListPlan(list))
				continue;
			Bunny::CommandListConfig config;
			if (SnapshotCommandListConfig(list, &config))
				RunCommandList(config, depth + 1, false, thisMatch);
		}
		for (const std::wstring &list : links.main) {
			if (!CommandListRuntimeEffect(list))
				continue;
			if (RunCompiledCommandListPlan(list))
				continue;
			Bunny::CommandListConfig config;
			if (SnapshotCommandListConfig(list, &config))
				RunCommandList(config, depth + 1, includePost, thisMatch);
		}
		if (includePost) {
			for (const std::wstring &list : links.post) {
				if (!CommandListRuntimeEffect(list))
					continue;
				if (RunCompiledCommandListPlan(list))
					continue;
				Bunny::CommandListConfig config;
				if (SnapshotCommandListConfig(list, &config))
					RunCommandList(config, depth + 1, true, thisMatch);
			}
		}
	}

	bool EnterCommandList(const std::wstring &name)
	{
		if (name.empty())
			return true;
		if (std::find(mCommandListStack.begin(), mCommandListStack.end(), name) ==
		    mCommandListStack.end()) {
			mCommandListStack.push_back(name);
			return true;
		}
#if defined(_DEBUG)
		DX12LogManager::JsonFunc("DX12CommandListCycleSuppressed",
			"\"section\":\"%S\",\"depth\":%zu", name.c_str(),
			mCommandListStack.size());
#endif
		return false;
	}

	void LeaveCommandList()
	{
		if (!mCommandListStack.empty())
			mCommandListStack.pop_back();
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12GraphicsCommandList *mCommandList = nullptr;
	const DX12IaHashState &mIaState;
	uint32_t mVertexCount = 0;
	uint32_t mIndexCount = 0;
	uint32_t mInstanceCount = 0;
	uint32_t mFirstVertex = 0;
	uint32_t mFirstIndex = 0;
	uint32_t mFirstInstance = 0;
	DX12ModIaReplacement *mReplacement = nullptr;
	std::vector<DX12PendingResourceViewRequest> *mPendingResourceViews = nullptr;
	bool mChanged = false;
	bool mExecuteCommands = true;
	bool mExecuteDrawActions = true;
	std::vector<std::wstring> mCommandListStack;
	MigotoCommandVmUtils::State mVm;
};

static bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews)
{
	if (!device || !commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffect(name))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, 0,
		replacement, pendingResourceViews);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
	return executor.Changed();
}

static bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost)
{
	if (name.empty())
		return false;
	if (!CommandListRuntimeEffect(name))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, nullptr, nullptr);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, includePost);
	return true;
}

static void ProcessDx12KeyBindingsForFrame()
{
	std::vector<Bunny::MigotoKeyBindingUtils::RuntimeAction> actions;
	AcquireSRWLockExclusive(&gModLock);
	if (gLoaded)
		Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
	ReleaseSRWLockExclusive(&gModLock);

#if defined(_DEBUG)
	if (!actions.empty())
		DX12LogManager::DebugJsonFunc("DX12KeyBindings",
			"\"actions\":%zu", actions.size());
#endif

	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
}

