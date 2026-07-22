static std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
LoadCommandExecutionRuntimeSnapshot()
{
	return std::atomic_load_explicit(
		&gCommandExecutionRuntimeSnapshot,
		std::memory_order_acquire);
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name)
{
	if (!snapshot)
		return true;
	auto it = snapshot->commandListRuntimeEffect.find(name);
	return it == snapshot->commandListRuntimeEffect.end() || it->second;
}

static bool CommandListRuntimeEffectLocked(
	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot,
	const std::wstring &name,
	bool includePost)
{
	if (!snapshot)
		return true;
	const auto &effects = includePost ?
		snapshot->commandListPostRuntimeEffect :
		snapshot->commandListPreRuntimeEffect;
	if (!effects.empty()) {
		auto it = effects.find(name);
		return it == effects.end() || it->second;
	}
	return CommandListRuntimeEffectLocked(snapshot, name);
}

static bool CommandListRuntimeEffect(const std::wstring &name)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name);
}

static bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name, includePost);
}

static std::shared_ptr<const Bunny::CommandListConfig>
SnapshotCommandListConfig(const std::wstring &name)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto it = snapshot->commandLists.find(name);
	return it == snapshot->commandLists.end() ? nullptr : it->second;
}

static std::shared_ptr<const DX12CompiledTextureOverridePlan>
SnapshotCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledTextureOverridePlans.find(config.section);
	return planIt == snapshot->compiledTextureOverridePlans.end() ?
		nullptr : planIt->second;
}

static std::shared_ptr<const DX12CompiledCommandListPlan>
SnapshotCompiledCommandListPlan(const std::wstring &name)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledCommandListPlans.find(name);
	return planIt == snapshot->compiledCommandListPlans.end() ?
		nullptr : planIt->second;
}

static std::shared_ptr<const DX12CompiledShaderOverridePlan>
SnapshotCompiledShaderOverridePlan(const std::wstring &section)
{
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> snapshot =
		LoadCommandExecutionRuntimeSnapshot();
	if (!snapshot)
		return nullptr;
	auto planIt = snapshot->compiledShaderOverridePlans.find(section);
	return planIt == snapshot->compiledShaderOverridePlans.end() ?
		nullptr : planIt->second;
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

	bool observedDescriptorTarget = false;
	bool observedDynamicCommandState = false;

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
		match.config = &config;
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
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			SnapshotCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePlan(*compiledPlan);
			return;
		}
		if (config.handlingSkip)
			SetSkip();
		RunCommandListLinks(config.commandLists, 0, false, nullptr);
		RunActions(config.actions, 0, false, nullptr);
	}

	void QueuePostTextureOverride(
		const DX12TextureOverrideMatch &match)
	{
		if (!mReplacement || !match.config || !match.postRuntimeEffect)
			return;
		DX12ModIaReplacement::QueuedTextureOverridePost queued;
		queued.config = match.config;
		queued.hasThisSrvTarget = match.hasThisSrvTarget;
		queued.compute = match.compute;
		queued.thisSrvBinding = match.thisSrvBinding;
		queued.matchedHash = match.matchedHash;
		queued.resourceHash = match.resourceHash;
		queued.descriptorHash = match.descriptorHash;
		mReplacement->AddQueuedPostTextureOverride(queued);
	}

	void RunQueuedPostTextureOverride(
		const DX12ModIaReplacement::QueuedTextureOverridePost &queued)
	{
		if (!queued.config)
			return;

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			SnapshotCompiledTextureOverridePlan(*queued.config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (compiledTextureOverride) {
			if (compiledPlanView->hasPostCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->postCommandListPlan, 0, true);
			return;
		}

		if (queued.config->commandLists.post.empty())
			return;

		DX12TextureOverrideMatch match;
		match.config = queued.config;
		match.preRuntimeEffect = true;
		match.postRuntimeEffect = true;
		match.hasThisSrvTarget = queued.hasThisSrvTarget;
		match.compute = queued.compute;
		match.thisSrvBinding = queued.thisSrvBinding;
		match.matchedHash = queued.matchedHash;
		match.resourceHash = queued.resourceHash;
		match.descriptorHash = queued.descriptorHash;
		Bunny::CommandListLinks postLinks;
		postLinks.main = queued.config->commandLists.post;
		RunCommandListLinks(postLinks, 0, true, &match);
	}

	void RunPostShaderOverrideLists(
		const Bunny::ShaderOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			SnapshotCompiledShaderOverridePlan(config.section);
		if (compiledPlan && !compiledPlan->unsupported) {
			RunCompiledShaderOverridePostPlan(*compiledPlan);
			return;
		}
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
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		if (target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
		    target.kind == Bunny::CommandListTargetKind::ShaderResource ||
		    target.kind == Bunny::CommandListTargetKind::UnorderedAccessView) {
			observedDescriptorTarget = true;
		}
		return ::FindTargetTextureOverride(
			mCommandList, mIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex, mFirstInstance, matches);
	}

	void RunTextureOverride(
		const DX12TextureOverrideMatch &match, int depth, bool includePost)
	{
		const Bunny::TextureOverrideConfig *config = match.config;
		if (!mReplacement || depth > 16 || !config)
			return;
		if (!includePost)
			QueuePostTextureOverride(match);

		const bool runPrePhase = includePost || match.preRuntimeEffect;
		if (!runPrePhase)
			return;

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			SnapshotCompiledTextureOverridePlan(*config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (compiledTextureOverride) {
			if (compiledPlanView->hasPreCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->preCommandListPlan, depth + 1, false);
			RunCompiledTextureOverridePlan(*compiledPlanView, depth, includePost);
			if (includePost && match.postRuntimeEffect &&
			    compiledPlanView->hasPostCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->postCommandListPlan, depth + 1, true);
		} else {
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.pre) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}
			}
			RunTextureOverrideActions(*config, depth, includePost, &match);
			if (mExecuteCommands) {
				for (const std::wstring &list : config->commandLists.main) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, false, &match);
				}

				if (includePost && match.postRuntimeEffect) {
					for (const std::wstring &list : config->commandLists.post) {
						Bunny::CommandListLinks links;
						links.main.push_back(list);
						RunCommandListLinks(links, depth + 1, true, &match);
					}
				}
			}
		}

		const bool needsGraphicsSrvBindings = compiledPlanView ?
			!compiledPlanView->drawSrvResources.empty() :
			(!config->drawPsSrvResources.empty() ||
			 !config->drawVsSrvResources.empty());
		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvBindings =
			needsGraphicsSrvBindings ? GetGraphicsSrvBindings() : nullptr;
		const bool drawSrvChanged = compiledPlanView ?
			DX12DrawSrvReplacementUtils::ApplyCompiledPlan(
				mDevice, mCommandList, *compiledPlanView, &match, mReplacement,
				graphicsSrvBindings) :
			DX12DrawSrvReplacementUtils::ApplyConfig(
				mDevice, mCommandList, *config, &match, mReplacement,
				graphicsSrvBindings);
		if (drawSrvChanged)
			MarkChanged();
	}

	bool RunCompiledTextureOverridePlan(
		const DX12CompiledTextureOverridePlan &plan, int depth, bool includePost)
	{
		if (plan.hasMainActionPlan) {
			ExecuteCompiledCommandListPlan(plan.mainActionPlan, depth, includePost);
			return true;
		}
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

	void RunCompiledShaderOverridePlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported)
			return;
		ExecuteCompiledCommandListPlan(plan.mainPlan, 0, false);
	}

	void RunCompiledShaderOverridePostPlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported || !plan.hasPostCommandListPlan)
			return;
		ExecuteCompiledCommandListPlan(plan.postCommandListPlan, 0, true);
	}

	bool RunCompiledCommandListPlan(
		const std::wstring &name, int depth, bool includePost)
	{
		if (!mReplacement)
			return false;
		if (!CommandListRuntimeEffect(name, includePost))
			return false;
		if (!EnterCommandList(name))
			return true;
		const std::shared_ptr<const DX12CompiledCommandListPlan> plan =
			SnapshotCompiledCommandListPlan(name);
		if (!plan) {
			LeaveCommandList();
			return false;
		}
		ExecuteCompiledCommandListPlan(*plan, depth, includePost);
		LeaveCommandList();
		return true;
	}

	void ExecuteCompiledCommandListPlan(
		const DX12CompiledCommandListPlan &plan, int depth, bool includePost)
	{
		const bool executeActions = mReplacement && (mExecuteCommands || mExecuteDrawActions);
		if (!plan.ops.empty()) {
			for (const DX12CompiledCommandListPlan::Op &op : plan.ops) {
				switch (op.kind) {
				case DX12CompiledCommandListPlan::Op::Kind::HandlingSkip:
					if (executeActions)
						SetSkip();
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetIndexBuffer:
					AppendResourceViews(
						op.resource,
						std::vector<DX12CompiledVertexResourceBinding>());
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetVertexBuffer:
					AppendResourceViews(
						L"",
						std::vector<DX12CompiledVertexResourceBinding>{
							{ op.target.slot, op.resource }});
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Draw:
					if (executeActions) {
						mReplacement->draws.push_back(op.draw);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::Dispatch:
					if (executeActions) {
						mReplacement->dispatches.push_back(op.dispatch);
						MarkChanged();
					}
					break;
				case DX12CompiledCommandListPlan::Op::Kind::CheckTextureOverride:
					if (!mExecuteCommands)
						break;
					{
						std::vector<DX12TextureOverrideMatch> matches;
						if (FindTargetTextureOverride(op.target, &matches)) {
							for (const DX12TextureOverrideMatch &match : matches)
								RunTextureOverride(match, depth + 1, includePost);
						}
					}
					break;
				}
			}
			return;
		}
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

	const std::vector<DX12CurrentShaderResourceBinding> *GetGraphicsSrvBindings()
	{
		if (!mCommandList)
			return nullptr;
		if (!mGraphicsSrvBindingsResolved) {
			mGraphicsSrvBindingsResolved = true;
			DX12DrawSrvReplacementUtils::CollectSrvBindings(
				mCommandList, false, &mGraphicsSrvBindings);
		}
		return &mGraphicsSrvBindings;
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
		if (RunCompiledCommandListPlan(commandList.section, depth, includePost))
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
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
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
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(action.commandList);
				if (config)
					RunCommandList(*config, depth + 1, includePost, thisMatch);
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
			observedDynamicCommandState = true;
			mVm.ApplyControlFlow(action.rawLine.empty() ? action.key : action.rawLine);
			return true;
		case Bunny::CommandListActionKind::VariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.Assign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::PostVariableAssignment:
			observedDynamicCommandState = true;
			if (includePost && mVm.Active())
				mVm.QueuePostAssign(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::LocalVariableAssignment:
			observedDynamicCommandState = true;
			if (mVm.Active())
				mVm.DeclareLocal(action.key, action.value);
			return true;
		case Bunny::CommandListActionKind::SetShaderConstant:
			observedDynamicCommandState = true;
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
			if (!CommandListRuntimeEffect(list, false))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, false))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, false, thisMatch);
		}
		for (const std::wstring &list : links.main) {
			if (!CommandListRuntimeEffect(list, includePost))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, includePost))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, includePost, thisMatch);
		}
		if (includePost) {
			for (const std::wstring &list : links.post) {
				if (!CommandListRuntimeEffect(list, true))
					continue;
				if (RunCompiledCommandListPlan(list, depth + 1, true))
					continue;
				const std::shared_ptr<const Bunny::CommandListConfig> config =
					SnapshotCommandListConfig(list);
				if (config)
					RunCommandList(*config, depth + 1, true, thisMatch);
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
		DX12LogManager::Fields("DX12CommandListCycleSuppressed",
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
	bool mGraphicsSrvBindingsResolved = false;
	std::vector<std::wstring> mCommandListStack;
	std::vector<DX12CurrentShaderResourceBinding> mGraphicsSrvBindings;
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
	if (!CommandListRuntimeEffect(name, true))
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
	if (!CommandListRuntimeEffect(name, includePost))
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
		DX12LogManager::DebugFields("DX12KeyBindings",
			"\"actions\":%zu", actions.size());
#endif

	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
}

static void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}

static bool ShaderModelMatchesRegex(
	const Bunny::ShaderRegexConfig &regex, const std::string &shaderModel)
{
	if (regex.hasPattern || shaderModel.empty())
		return false;
	for (const std::string &model : regex.shaderModels) {
		if (model == shaderModel)
			return true;
	}
	return false;
}

static constexpr LONG ShaderOverrideHotPathLogLimit = 512;
static volatile LONG gShaderOverridePsoCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheHitLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheStoreLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDescriptorBlockedLogCount = 0;
static volatile LONG gShaderOverrideNegativeCacheDynamicBlockedLogCount = 0;

static bool AllowShaderOverrideHotPathLog(volatile LONG *counter)
{
#if defined(_DEBUG)
	if (!DX12LogManager::DiagnosticsLoggingEnabled())
		return false;
	return InterlockedIncrement(counter) <= ShaderOverrideHotPathLogLimit;
#else
	(void)counter;
	return false;
#endif
}

static Bunny::ShaderOverrideConfig MakeShaderOverrideFromRegex(
	const Bunny::ShaderRegexConfig &regex, uint64_t hash)
{
	Bunny::ShaderOverrideConfig config;
	config.section = regex.section;
	config.originalSection = regex.originalSection;
	config.sourcePath = regex.sourcePath;
	config.sourceDir = regex.sourceDir;
	config.iniNamespace = regex.iniNamespace;
	config.hash = hash;
	config.handlingSkip = regex.handlingSkip;
	config.commandLists = regex.commandLists;
	config.actions = regex.actions;
	return config;
}

static void AppendShaderRegexForShaderLocked(
	uint64_t hash,
	const std::string &shaderModel,
	std::vector<Bunny::ShaderOverrideConfig> *regexConfigs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || shaderModel.empty() || !regexConfigs || !seen)
		return;

	for (const auto &item : gShaderRegexes) {
		const Bunny::ShaderRegexConfig &regex = item.second;
		if (!ShaderModelMatchesRegex(regex, shaderModel))
			continue;
		if (!seen->insert(regex.section).second)
			continue;
		regexConfigs->push_back(MakeShaderOverrideFromRegex(regex, hash));
#if defined(_DEBUG)
		if (DX12LogManager::DiagnosticsLoggingEnabled()) {
			DX12LogManager::DebugFields("DX12ShaderRegexMatch",
				"\"section\":\"%S\",\"hash\":\"%016llx\",\"model\":\"%S\"",
				regex.section.c_str(), static_cast<unsigned long long>(hash),
				std::wstring(shaderModel.begin(), shaderModel.end()).c_str());
		}
#endif
	}
}

static void BuildShaderOverrideConfigPointers(
	DX12ShaderOverridePsoMatchCache *cache,
	bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!cache || !configs)
		return;
	configs->clear();
	const std::vector<const Bunny::ShaderOverrideConfig*> &direct =
		dispatch ? cache->dispatchConfigs : cache->drawConfigs;
	configs->insert(configs->end(), direct.begin(), direct.end());
	std::vector<Bunny::ShaderOverrideConfig> &regex =
		dispatch ? cache->regexDispatchConfigs : cache->regexDrawConfigs;
	for (Bunny::ShaderOverrideConfig &config : regex)
		configs->push_back(&config);
}

static void CopyShaderOverrideConfigs(
	const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	std::vector<Bunny::ShaderOverrideConfig> *snapshot)
{
	if (!snapshot)
		return;
	snapshot->clear();
	snapshot->reserve(configs.size());
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (config)
			snapshot->push_back(*config);
	}
}

class DX12ShaderOverrideConfigView
{
public:
	void SetOwnedSnapshot(
		const std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> &snapshot)
	{
		mOwnedSnapshot = snapshot;
		mFallbackSnapshot.clear();
	}

	std::vector<Bunny::ShaderOverrideConfig> *MutableFallbackSnapshot()
	{
		mOwnedSnapshot.reset();
		return &mFallbackSnapshot;
	}

	const std::vector<Bunny::ShaderOverrideConfig> &Get() const
	{
		return mOwnedSnapshot ? *mOwnedSnapshot : mFallbackSnapshot;
	}

private:
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> mOwnedSnapshot;
	std::vector<Bunny::ShaderOverrideConfig> mFallbackSnapshot;
};

// PSO-Create-Time pre-compiled override plan: snapshots all shader override configs
// for a PSO so draw-time discovery and per-draw deep copy are eliminated.
// Populated in RecordGraphicsPipelineState; consumed in PrepareShaderOverrideReplacement.
class DX12CompiledPsoOverridePlan
{
public:
	bool valid = false;
	bool alwaysMiss = false;
	UINT64 generation = 0;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> configSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
};

static std::unordered_map<ID3D12PipelineState*, DX12CompiledPsoOverridePlan> gPsoOverridePlans;
static SRWLOCK gPsoOverridePlansLock = SRWLOCK_INIT;

static bool TryLoadCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> *configSnapshot,
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> *compiledPlanSnapshot,
	bool *alwaysMiss)
{
	if (!configSnapshot || !compiledPlanSnapshot || !alwaysMiss)
		return false;
	*alwaysMiss = false;
	configSnapshot->reset();
	compiledPlanSnapshot->reset();
	if (!pipelineState)
		return false;

	AcquireSRWLockShared(&gPsoOverridePlansLock);
	auto it = gPsoOverridePlans.find(pipelineState);
	if (it == gPsoOverridePlans.end() || it->second.generation != reloadGeneration) {
		ReleaseSRWLockShared(&gPsoOverridePlansLock);
		return false;
	}
	*alwaysMiss = it->second.alwaysMiss;
	if (!it->second.alwaysMiss) {
		*configSnapshot = it->second.configSnapshot;
		*compiledPlanSnapshot = it->second.compiledPlanSnapshot;
	}
	ReleaseSRWLockShared(&gPsoOverridePlansLock);
	return true;
}

static std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>>
BuildCompiledShaderOverridePlanSnapshot(
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	if (configSnapshot.empty())
		return nullptr;
	std::shared_ptr<std::vector<DX12CompiledShaderOverridePlan>> compiledPlans =
		std::make_shared<std::vector<DX12CompiledShaderOverridePlan>>();
	compiledPlans->reserve(configSnapshot.size());
	for (const Bunny::ShaderOverrideConfig &config : configSnapshot) {
		DX12CompiledShaderOverridePlan plan;
		BuildCompiledShaderOverridePlanFromParts(
			config.handlingSkip,
			config.commandLists,
			config.actions,
			gCompiledCommandListPlans,
			&plan);
		if (plan.unsupported)
			return nullptr;
		compiledPlans->push_back(std::move(plan));
	}
	return compiledPlans;
}

static void StoreCompiledPsoPlan(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot)
{
	// Never store empty plans: PSOs created before mod loads would cache "alwaysMiss"
	// and never recover when the mod later loads matching configs.
	if (!pipelineState || configSnapshot.empty())
		return;

	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	DX12CompiledPsoOverridePlan plan;
	plan.valid = true;
	plan.alwaysMiss = false;
	plan.generation = reloadGeneration;
	plan.configSnapshot = std::make_shared<const std::vector<Bunny::ShaderOverrideConfig>>(configSnapshot);
	plan.compiledPlanSnapshot =
		BuildCompiledShaderOverridePlanSnapshot(configSnapshot);
	gPsoOverridePlans[pipelineState] = plan;
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

static void ClearCompiledPsoPlans()
{
	AcquireSRWLockExclusive(&gPsoOverridePlansLock);
	gPsoOverridePlans.clear();
	ReleaseSRWLockExclusive(&gPsoOverridePlansLock);
}

class DX12ShaderOverrideIaTargetPlanUtils
{
public:
	static bool CommandListRuntimeEffectLocked(const std::wstring &name)
	{
		std::unordered_map<std::wstring, bool>::const_iterator found =
			gCommandListPreRuntimeEffect.find(name);
		return found == gCommandListPreRuntimeEffect.end() || found->second;
	}

	static void RequireTargetLocked(
		const Bunny::CommandListTarget &target,
		DX12IaHashTargetPlan *targetPlan)
	{
		if (!targetPlan)
			return;

		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			targetPlan->RequireIndexBuffer();
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			targetPlan->RequireVertexBuffer(target.slot);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			targetPlan->RequireDescriptorTarget();
			break;
		default:
			targetPlan->RequireAll();
			break;
		}
	}

	static void CollectCommandListLocked(
		const std::wstring &name,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets);

	static void CollectActionsLocked(
		const std::vector<Bunny::CommandListAction> &actions,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const Bunny::CommandListAction &action : actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run:
				CollectCommandListLocked(
					action.commandList, depth + 1, commandListStack,
					targetPlan, textureOverrideTargetsCollected,
					collectTextureOverrideTargets);
				break;
			case Bunny::CommandListActionKind::CheckTextureOverride:
				RequireTargetLocked(action.target, targetPlan);
				if (collectTextureOverrideTargets &&
				    textureOverrideTargetsCollected &&
				    !*textureOverrideTargetsCollected) {
					*textureOverrideTargetsCollected = true;
					CollectAllTextureOverrideTargetsLocked(
						commandListStack, targetPlan,
						textureOverrideTargetsCollected);
				}
				break;
			default:
				break;
			}
		}
	}

	static void CollectCommandListLinksLocked(
		const Bunny::CommandListLinks &links,
		bool includePost,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const std::wstring &name : links.pre) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
		for (const std::wstring &name : links.main) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
		if (!includePost)
			return;
		for (const std::wstring &name : links.post) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets);
		}
	}

	static void CollectShaderOverrideConfigLocked(
		const Bunny::ShaderOverrideConfig &config,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool includePostLinks)
	{
		CollectCommandListLinksLocked(
			config.commandLists, includePostLinks, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets);
		CollectActionsLocked(
			config.actions, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets);
	}

	static void CollectAllTextureOverrideTargetsLocked(
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected)
	{
		if (!targetPlan)
			return;
		if (textureOverrideTargetsCollected)
			*textureOverrideTargetsCollected = true;

		Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
		for (; bucket != gTextureOverrides.end(); ++bucket) {
			const Bunny::TextureOverrideList &configs = bucket->second;
			for (size_t index = 0; index < configs.size(); ++index) {
				const Bunny::TextureOverrideConfig &config = configs[index];
				CollectCommandListLinksLocked(
					config.commandLists, true, 0, commandListStack,
					targetPlan, textureOverrideTargetsCollected, false);
				CollectActionsLocked(
					config.actions, 0, commandListStack, targetPlan,
					textureOverrideTargetsCollected, false);
			}
		}
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const DX12ShaderOverridePsoMatchCache &cache,
		bool collectTextureOverrideTargets)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig *config : cache.drawConfigs) {
			if (!config)
				continue;
			CollectShaderOverrideConfigLocked(
				*config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false);
		}
		for (const Bunny::ShaderOverrideConfig &config : cache.regexDrawConfigs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false);
		}
		return targetPlan;
	}
};

inline void DX12ShaderOverrideIaTargetPlanUtils::CollectCommandListLocked(
	const std::wstring &name,
	UINT depth,
	std::unordered_set<std::wstring> *commandListStack,
	DX12IaHashTargetPlan *targetPlan,
	bool *textureOverrideTargetsCollected,
	bool collectTextureOverrideTargets)
{
	if (!targetPlan || name.empty() || depth > 16)
		return;
	if (!CommandListRuntimeEffectLocked(name))
		return;
	if (commandListStack) {
		if (commandListStack->find(name) != commandListStack->end())
			return;
		commandListStack->insert(name);
	}

	Bunny::CommandListMap::const_iterator found = gCommandLists.find(name);
	if (found != gCommandLists.end()) {
		CollectActionsLocked(
			found->second.actions, depth + 1, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets);
	}

	if (commandListStack)
		commandListStack->erase(name);
}

static void LogShaderOverrideCommandListSnapshotLimited(
	const char *phase, const std::vector<Bunny::ShaderOverrideConfig> &configs,
	const DX12ModIaReplacement &replacement)
{
#if defined(_DEBUG)
	std::vector<const Bunny::ShaderOverrideConfig*> pointers;
	pointers.reserve(configs.size());
	for (const Bunny::ShaderOverrideConfig &config : configs)
		pointers.push_back(&config);
	LogShaderOverrideCommandListLimited(phase, pointers, replacement);
#else
	(void)phase;
	(void)configs;
	(void)replacement;
#endif
}

static bool ShaderOverrideNegativeCacheEnabled(
	bool allowDescriptorIndependentNegativeCache)
{
	return allowDescriptorIndependentNegativeCache &&
		DX12ModHasActiveShaderOverrides();
}

static DX12ShaderOverrideNegativeCacheKey MakeShaderOverrideNegativeCacheKey(
	ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance)
{
	DX12ShaderOverrideNegativeCacheKey key = {};
	key.reloadGeneration = static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gShaderOverrideNegativeCacheGeneration), 0, 0));
	key.preSkinGeneration = CurrentPreSkinActiveGeneration();
	key.pipelineState = pipelineState;
	key.vertexCount = vertexCount;
	key.indexCount = indexCount;
	key.instanceCount = instanceCount;
	key.firstVertex = firstVertex;
	key.firstIndex = firstIndex;
	key.firstInstance = firstInstance;
	key.hasIndexBuffer = iaState.hasIndexBuffer;
	key.indexHash = iaState.hasIndexBuffer ? iaState.indexHash : 0;
	if (iaState.hasIndexBuffer) {
		key.indexBufferSize = iaState.indexView.SizeInBytes;
		key.indexBufferFormat = static_cast<uint32_t>(iaState.indexView.Format);
	}
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot < key.vertexHashes.size()) {
			key.vertexHashes[buffer.slot] = buffer.hash;
			key.vertexPresent[buffer.slot] = 1;
			key.vertexBufferSizes[buffer.slot] = buffer.vertexView.SizeInBytes;
			key.vertexBufferStrides[buffer.slot] = buffer.vertexView.StrideInBytes;
		}
	}
	return key;
}

static UINT64 HashShaderOverrideNegativeCacheKey(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	UINT64 hash = 0x28cb7a8d31e7b4c5ull;
	hash = HashCombine64(hash, key.reloadGeneration);
	hash = HashCombine64(hash, key.preSkinGeneration);
	hash = HashCombine64(hash, reinterpret_cast<uintptr_t>(key.pipelineState));
	hash = HashCombine64(hash, key.vertexCount);
	hash = HashCombine64(hash, key.indexCount);
	hash = HashCombine64(hash, key.instanceCount);
	hash = HashCombine64(hash, key.firstVertex);
	hash = HashCombine64(hash, key.firstIndex);
	hash = HashCombine64(hash, key.firstInstance);
	hash = HashCombine64(hash, key.hasIndexBuffer ? 1 : 0);
	hash = HashCombine64(hash, key.indexHash);
	hash = HashCombine64(hash, key.indexBufferSize);
	hash = HashCombine64(hash, key.indexBufferFormat);
	for (size_t index = 0; index < key.vertexHashes.size(); ++index) {
		hash = HashCombine64(hash, key.vertexHashes[index]);
		hash = HashCombine64(hash, key.vertexPresent[index]);
		hash = HashCombine64(hash, key.vertexBufferSizes[index]);
		hash = HashCombine64(hash, key.vertexBufferStrides[index]);
	}
	return hash ? hash : 1;
}

static bool TryFindShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	AcquireSRWLockShared(&gShaderOverrideNegativeCacheLock);
	auto it = gShaderOverrideNegativeCache.find(hash);
	const bool found = it != gShaderOverrideNegativeCache.end() &&
		it->second == key;
	ReleaseSRWLockShared(&gShaderOverrideNegativeCacheLock);
	return found;
}

static void StoreShaderOverrideNegativeCache(
	const DX12ShaderOverrideNegativeCacheKey &key)
{
	const UINT64 hash = HashShaderOverrideNegativeCacheKey(key);
	AcquireSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
	gShaderOverrideNegativeCache[hash] = key;
	ReleaseSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
}

static void ClearShaderOverrideNegativeCache()
{
	AcquireSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
	gShaderOverrideNegativeCache.clear();
	ReleaseSRWLockExclusive(&gShaderOverrideNegativeCacheLock);
}

static bool TryFindCachedShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!configs)
		return true;
	configs->clear();
	if (!pipelineState)
		return true;

	auto cached = gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached != gShaderOverridePsoMatchCache.end() &&
	    cached->second.generation == gReloadGeneration) {
		BuildShaderOverrideConfigPointers(&cached->second, dispatch, configs);
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(&gShaderOverridePsoCacheHitLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverridePsoMatchCache",
				"\"status\":\"hit\",\"pso\":\"%p\",\"dispatch\":%s,\"matches\":%zu",
				pipelineState, dispatch ? "true" : "false", configs->size());
		}
#endif
		return true;
	}
	return false;
}

// Public draw-path PSO plan lookup used before IA hash work.
// This avoids building IA hashes for PSOs with no matching shader overrides.
static bool TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawTargetPlan;
	return true;
}

static bool TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan || !pipelineState)
		return false;
	std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache>::iterator cached =
		gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached == gShaderOverridePsoMatchCache.end() ||
	    cached->second.generation != gReloadGeneration)
		return false;
	*targetPlan = cached->second.drawDirectTargetPlan;
	return true;
}

bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState)
		return false;
	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	bool cached = false;
	AcquireSRWLockShared(&gModLock);
	cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);
	return !cached || !configs.empty();
}

static void FindShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (TryFindCachedShaderOverridesForPsoLocked(pipelineState, dispatch, configs))
		return;

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return;

	std::unordered_set<std::wstring> seen;
	std::unordered_set<std::wstring> regexSeen;
	DX12ShaderOverridePsoMatchCache matchCache;
	matchCache.generation = gReloadGeneration;
	if (dispatch) {
		if (info.hasCS) {
			AppendShaderOverrideForHashLocked(info.cs, &matchCache.dispatchConfigs, &seen);
			AppendShaderRegexForShaderLocked(
				info.cs, info.csModel,
				&matchCache.regexDispatchConfigs, &regexSeen);
		}
		DX12ShaderOverridePsoMatchCache &stored =
			gShaderOverridePsoMatchCache[pipelineState];
		stored = std::move(matchCache);
		BuildShaderOverrideConfigPointers(&stored, dispatch, configs);
		return;
	}

	if (info.hasVS) {
		AppendShaderOverrideForHashLocked(info.vs, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.vs, info.vsModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	if (info.hasPS) {
		AppendShaderOverrideForHashLocked(info.ps, &matchCache.drawConfigs, &seen);
		AppendShaderRegexForShaderLocked(
			info.ps, info.psModel,
			&matchCache.regexDrawConfigs, &regexSeen);
	}
	matchCache.drawDirectTargetPlan =
		DX12ShaderOverrideIaTargetPlanUtils::BuildDrawPlanLocked(matchCache, false);
	// Keep the draw-decision miss path focused on the first-step CTO targets.
	// Nested TextureOverride CTOs are now executed by the compiled command-list op
	// stream on actual matches, so globally unioning every reachable TO target
	// here only inflates descriptor/IA requirements for negative draws.
	matchCache.drawTargetPlan = matchCache.drawDirectTargetPlan;
	gShaderOverridePsoMatchCache[pipelineState] = std::move(matchCache);
	BuildShaderOverrideConfigPointers(
		&gShaderOverridePsoMatchCache[pipelineState], dispatch, configs);
}

bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan)
{
	if (!targetPlan)
		return false;
	targetPlan->RequireAll();
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	AcquireSRWLockShared(&gModLock);
	const bool cachedPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockShared(&gModLock);
	if (cachedPlan)
		return true;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	AcquireSRWLockExclusive(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	const bool builtPlan =
		TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
			pipelineState, targetPlan);
	ReleaseSRWLockExclusive(&gModLock);
	return builtPlan;
}

bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision)
{
	if (!decision)
		return false;

	const bool activeShaderOverrides = DX12ModHasActiveShaderOverrides();
	const UINT64 reloadGeneration = DX12ModGetReloadGeneration();
	if (!pipelineState || !activeShaderOverrides) {
		decision->SetNoopNeverMatches(
			pipelineState, reloadGeneration, activeShaderOverrides);
		return true;
	}

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;
	targetPlan.RequireAll();
	directTargetPlan.RequireAll();
	bool cached = false;
	bool targetPlanResolved = false;
	bool directTargetPlanResolved = false;

	AcquireSRWLockShared(&gModLock);
	cached = TryFindCachedShaderOverridesForPsoLocked(
		pipelineState, false, &configs);
	if (cached) {
		targetPlanResolved =
			TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
				pipelineState, &targetPlan);
		directTargetPlanResolved =
			TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
				pipelineState, &directTargetPlan);
	}
	ReleaseSRWLockShared(&gModLock);

	if (!cached) {
		AcquireSRWLockExclusive(&gModLock);
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		cached = TryFindCachedShaderOverridesForPsoLocked(
			pipelineState, false, &configs);
		if (cached) {
			targetPlanResolved =
				TryFindCachedShaderOverrideIaTargetPlanForPsoLocked(
					pipelineState, &targetPlan);
			directTargetPlanResolved =
				TryFindCachedShaderOverrideDirectIaTargetPlanForPsoLocked(
					pipelineState, &directTargetPlan);
		}
		ReleaseSRWLockExclusive(&gModLock);
	}

	const bool shaderOverrides = cached ? !configs.empty() : true;
	if (!targetPlanResolved)
		targetPlan.RequireAll();
	if (!directTargetPlanResolved)
		directTargetPlan = targetPlan;

	const bool shaderOverrideSkip =
		DX12ModShouldSkipPipelineState(pipelineState, false);
	const bool descriptorTrackingNeeded =
		DX12ModNeedsShaderDescriptorTracking();
	decision->SetFromTargetPlan(
		pipelineState, reloadGeneration, activeShaderOverrides,
		shaderOverrides, shaderOverrideSkip, targetPlan, directTargetPlan,
		descriptorTrackingNeeded);
	return true;
}

bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance)
{
	if (!targetPlan.hasTextureOverrideChecks ||
	    targetPlan.hasDescriptorTargets ||
	    targetPlan.requireAllIaBuffers ||
	    !targetPlan.HasIaTargets()) {
		return true;
	}

	Bunny::CommandListTarget target;
	target.stage = Bunny::CommandListShaderStage::Unknown;
	if (targetPlan.RequiresIndexBuffer(indexed) &&
	    iaState.hasIndexBuffer &&
	    iaState.indexHash) {
		target.kind = Bunny::CommandListTargetKind::IndexBuffer;
		target.slot = 0;
		if (FindIaTextureOverridesCached(
			    target,
			    iaState.indexHash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    true,
				    0,
				    nullptr)) {
				return true;
			}
		}

	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!targetPlan.RequiresVertexBuffer(buffer.slot))
			continue;
		if (!buffer.hash)
			continue;

		target.kind = Bunny::CommandListTargetKind::VertexBuffer;
		target.slot = buffer.slot;
		if (FindIaTextureOverridesCached(
			    target,
			    buffer.hash,
			    vertexCount,
			    indexCount,
			    instanceCount,
			    firstVertex,
				    firstIndex,
				    firstInstance,
				    false,
				    buffer.slot,
				    nullptr)) {
				return true;
			}
		}
	return false;
}

bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement,
	bool allowDescriptorIndependentNegativeCache,
	ID3D12Device *cachedDevice)
{
	if (!replacement)
		return false;
	*replacement = DX12ModIaReplacement();
	if (!commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	const bool negativeCacheEnabled = ShaderOverrideNegativeCacheEnabled(
		allowDescriptorIndependentNegativeCache);
	DX12ShaderOverrideNegativeCacheKey negativeCacheKey = {};
	if (negativeCacheEnabled) {
		negativeCacheKey = MakeShaderOverrideNegativeCacheKey(
			pipelineState, iaState,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance);
		if (TryFindShaderOverrideNegativeCache(negativeCacheKey)) {
#if defined(_DEBUG)
			if (AllowShaderOverrideHotPathLog(&gShaderOverrideNegativeCacheHitLogCount)) {
				DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
					"\"status\":\"hit\",\"pso\":\"%p\"", pipelineState);
			}
#endif
			return false;
		}
	}

	// Try the precompiled PSO plan first (zero-lock, zero-discovery).
	// Eliminates per-draw lock+lookup+copy for PSOs recorded after mod loading.
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	DX12ShaderOverrideConfigView configView;
	std::shared_ptr<const std::vector<Bunny::ShaderOverrideConfig>> compiledSnapshot;
	std::shared_ptr<const std::vector<DX12CompiledShaderOverridePlan>> compiledPlanSnapshot;
	bool alwaysMiss = false;
	bool compiledPlanHit = TryLoadCompiledPsoPlan(
		pipelineState, reloadGeneration,
		&compiledSnapshot, &compiledPlanSnapshot, &alwaysMiss);
	if (compiledPlanHit && alwaysMiss)
		return false;
	if (compiledPlanHit) {
		configView.SetOwnedSnapshot(compiledSnapshot);
		if (configView.Get().empty())
			compiledPlanHit = false;
	}
	bool cached = compiledPlanHit;

	if (!compiledPlanHit) {
		// Fallback: legacy PSO config cache lookup (first draw after mod reload).
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		AcquireSRWLockShared(&gModLock);
		cached = TryFindCachedShaderOverridesForPsoLocked(pipelineState, false, &configs);
		if (cached)
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
		ReleaseSRWLockShared(&gModLock);

		if (cached && configView.Get().empty())
			return false;

		if (!cached) {
			AcquireSRWLockExclusive(&gModLock);
			FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
			CopyShaderOverrideConfigs(configs, configView.MutableFallbackSnapshot());
			ReleaseSRWLockExclusive(&gModLock);
		}
		if (configView.Get().empty())
			return false;
	}
	const std::vector<Bunny::ShaderOverrideConfig> &configSnapshot = configView.Get();

	ID3D12Device *device = AcquireModDeviceFast(commandList, cachedDevice);
	const bool deviceFromCache = (cachedDevice != nullptr);
	if (!device)
		return false;
	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	if (compiledPlanSnapshot && !compiledPlanSnapshot->empty()) {
		for (const DX12CompiledShaderOverridePlan &plan : *compiledPlanSnapshot)
			executor.RunCompiledShaderOverridePlan(plan);
	} else {
		for (const Bunny::ShaderOverrideConfig &config : configSnapshot)
			executor.RunShaderOverride(config);
	}
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	LogShaderOverrideCommandListSnapshotLimited("pre", configSnapshot, *replacement);
	bool changed = executor.Changed() || replacement->skip || !replacement->draws.empty() ||
		!replacement->dispatches.empty();
	if (!deviceFromCache)
		device->Release();

	const bool negativeCacheStoreSafe = negativeCacheEnabled && !changed &&
		pendingResourceViews.empty() && !executor.observedDescriptorTarget &&
		!executor.observedDynamicCommandState;
	if (negativeCacheStoreSafe) {
		StoreShaderOverrideNegativeCache(negativeCacheKey);
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(&gShaderOverrideNegativeCacheStoreLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"store\",\"pso\":\"%p\"", pipelineState);
		}
#endif
	} else if (negativeCacheEnabled && !changed && executor.observedDescriptorTarget) {
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(
				&gShaderOverrideNegativeCacheDescriptorBlockedLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"descriptor_blocked\",\"pso\":\"%p\"",
				pipelineState);
		}
#endif
	} else if (negativeCacheEnabled && !changed && executor.observedDynamicCommandState) {
#if defined(_DEBUG)
		if (AllowShaderOverrideHotPathLog(
				&gShaderOverrideNegativeCacheDynamicBlockedLogCount)) {
			DX12LogManager::DebugFields("DX12ShaderOverrideNegativeCache",
				"\"status\":\"dynamic_state_blocked\",\"pso\":\"%p\"",
				pipelineState);
		}
#endif
	}

	return changed;
}

void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || !pipelineState || !DX12ModHasActiveShaderOverrides())
		return;
	if (replacement->postCommandLists.empty() &&
	    replacement->queuedPostTextureOverrides.empty())
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		replacement, &pendingResourceViews);
	executor.SetExecutionMode(true, true);
	for (const std::wstring &list : replacement->postCommandLists)
		executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { list }, {} }, true);
	for (const DX12ModIaReplacement::QueuedTextureOverridePost &queued :
	     replacement->queuedPostTextureOverrides)
		executor.RunQueuedPostTextureOverride(queued);
	const bool resourcesChanged = ApplyPendingResourceViewsForCommandList(
		device, commandList, iaState, pendingResourceViews, replacement);
	if (resourcesChanged)
		executor.MarkExternalChange();
	device->Release();
}

static bool ShaderOverrideHasSkipLocked(uint64_t hash)
{
	auto it = gShaderOverrides.find(hash);
	return it != gShaderOverrides.end() && it->second.handlingSkip;
}

static bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode)
{
	if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
		return false;
	return ShaderOverrideHasSkipLocked(
		DX12ModHashShaderBytecode(bytecode.pShaderBytecode, bytecode.BytecodeLength));
}

static bool StoredPsoHasSkipLocked(const DX12StoredPso &record, bool dispatch)
{
	if (dispatch) {
		return record.kind == DX12PsoKind::Compute &&
			ShaderBytecodeHasSkipLocked(record.computeDesc.CS);
	}

	return record.kind == DX12PsoKind::Graphics &&
		(ShaderBytecodeHasSkipLocked(record.graphicsDesc.VS) ||
		 ShaderBytecodeHasSkipLocked(record.graphicsDesc.PS));
}

static void UpdateStoredPsoSkipLocked(DX12StoredPso *record)
{
	if (!record || record->skipGeneration == gReloadGeneration)
		return;

	record->skipDraw = false;
	record->skipDispatch = false;
	if (record->kind == DX12PsoKind::Compute) {
		record->skipDispatch = ShaderBytecodeHasSkipLocked(record->computeDesc.CS);
	} else {
		record->skipDraw =
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.VS) ||
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.PS);
	}
	record->skipGeneration = gReloadGeneration;
}

bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return false;

	bool skip = false;
	AcquireSRWLockShared(&gModLock);
	auto record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		if (record->second.skipGeneration == gReloadGeneration) {
			skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
			ReleaseSRWLockShared(&gModLock);
			return skip;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		UpdateStoredPsoSkipLocked(&record->second);
		skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
		ReleaseSRWLockExclusive(&gModLock);
		return skip;
	}
	ReleaseSRWLockExclusive(&gModLock);

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return false;

	AcquireSRWLockShared(&gModLock);
	if (dispatch) {
		skip = info.hasCS && ShaderOverrideHasSkipLocked(info.cs);
	} else {
		skip = (info.hasVS && ShaderOverrideHasSkipLocked(info.vs)) ||
			(info.hasPS && ShaderOverrideHasSkipLocked(info.ps));
	}
	ReleaseSRWLockShared(&gModLock);
	return skip;
}

UINT64 DX12ModGetReloadGeneration()
{
	AcquireSRWLockShared(&gModLock);
	UINT64 generation = gReloadGeneration;
	ReleaseSRWLockShared(&gModLock);
	return generation;
}

UINT64 DX12ModGetReloadGenerationFast()
{
	return static_cast<UINT64>(InterlockedCompareExchange64(
		reinterpret_cast<volatile LONG64*>(&gReloadGeneration), 0, 0));
}

void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Graphics;
	record.device = device;
	record.device->AddRef();
	DeepCopyGraphicsDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile PSO configuration and store the precompiled plan.
	// Eliminates per-draw lock+lookup+copy for subsequent draws.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Compute;
	record.device = device;
	record.device->AddRef();
	DeepCopyComputeDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);

	// Eagerly compile configuration for compute pipelines as well.
	if (DX12ModHasActiveShaderOverrides()) {
		std::vector<const Bunny::ShaderOverrideConfig*> configs;
		FindShaderOverridesForPsoLocked(pipelineState, true, &configs);
		std::vector<Bunny::ShaderOverrideConfig> snapshot;
		CopyShaderOverrideConfigs(configs, &snapshot);
		StoreCompiledPsoPlan(pipelineState, gReloadGeneration, snapshot);
	}

	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

static bool HasShaderOverrideLocked(uint64_t hash)
{
	return gShaderOverrides.find(hash) != gShaderOverrides.end();
}

static bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc)
{
	if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)))
		return true;
	if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)))
		return true;
	return false;
}

static bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc)
{
	return desc.CS.pShaderBytecode && desc.CS.BytecodeLength &&
		HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.CS.pShaderBytecode, desc.CS.BytecodeLength));
}

static ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = record->graphicsDesc;
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;

	if (DX12ModReplaceShaderBytecode("vs", desc.VS, &vs, &vsBytes)) {
		desc.VS = vs;
		changed = true;
	}
	if (DX12ModReplaceShaderBytecode("ps", desc.PS, &ps, &psBytes)) {
		desc.PS = ps;
		changed = true;
	}
	if (!changed)
		return nullptr;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateGraphicsPipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogManager::DebugFields("DX12ReplacementPsoCreate",
		"\"kind\":\"graphics\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

static ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = record->computeDesc;
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	if (!DX12ModReplaceShaderBytecode("cs", desc.CS, &cs, &csBytes))
		return nullptr;
	desc.CS = cs;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12DeviceHooks::CreateComputePipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogManager::DebugFields("DX12ReplacementPsoCreate",
		"\"kind\":\"compute\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState || !DX12ModHasActiveShaderOverrides())
		return nullptr;

	DX12StoredPso createRecord;
	bool shouldCreate = false;
	UINT64 generation = 0;

	AcquireSRWLockShared(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator cached =
		gPsoRecords.find(pipelineState);
	if (cached == gPsoRecords.end()) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	generation = gReloadGeneration;
	if (cached->second.replacement && cached->second.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = cached->second.replacement;
		ReleaseSRWLockShared(&gModLock);
		return replacement;
	}
	if (cached->second.replacementNoopGeneration == generation) {
		ReleaseSRWLockShared(&gModLock);
		return nullptr;
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	std::unordered_map<ID3D12PipelineState*, DX12StoredPso>::iterator it =
		gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end()) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	DX12StoredPso &record = it->second;
	generation = gReloadGeneration;
	if (record.replacement && record.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = record.replacement;
		ReleaseSRWLockExclusive(&gModLock);
		return replacement;
	}
	if (record.replacementNoopGeneration == generation) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	if (record.replacement) {
		record.replacement->Release();
		record.replacement = nullptr;
		record.replacementGeneration = 0;
	}

	bool needsReplacement = record.kind == DX12PsoKind::Graphics ?
		GraphicsPsoNeedsReplacementLocked(record.graphicsDesc) :
		ComputePsoNeedsReplacementLocked(record.computeDesc);
	if (!needsReplacement) {
		record.replacementNoopGeneration = generation;
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}
	record.replacementNoopGeneration = 0;

	createRecord.kind = record.kind;
	createRecord.device = record.device;
	createRecord.graphicsRootSignature = record.graphicsRootSignature;
	createRecord.computeRootSignature = record.computeRootSignature;
	createRecord.graphicsDesc = record.graphicsDesc;
	createRecord.computeDesc = record.computeDesc;
	createRecord.vsBytecode = record.vsBytecode;
	createRecord.psBytecode = record.psBytecode;
	createRecord.dsBytecode = record.dsBytecode;
	createRecord.hsBytecode = record.hsBytecode;
	createRecord.gsBytecode = record.gsBytecode;
	createRecord.csBytecode = record.csBytecode;
	if (!createRecord.vsBytecode.empty())
		createRecord.graphicsDesc.VS.pShaderBytecode = createRecord.vsBytecode.data();
	if (!createRecord.psBytecode.empty())
		createRecord.graphicsDesc.PS.pShaderBytecode = createRecord.psBytecode.data();
	if (!createRecord.dsBytecode.empty())
		createRecord.graphicsDesc.DS.pShaderBytecode = createRecord.dsBytecode.data();
	if (!createRecord.hsBytecode.empty())
		createRecord.graphicsDesc.HS.pShaderBytecode = createRecord.hsBytecode.data();
	if (!createRecord.gsBytecode.empty())
		createRecord.graphicsDesc.GS.pShaderBytecode = createRecord.gsBytecode.data();
	if (!createRecord.csBytecode.empty())
		createRecord.computeDesc.CS.pShaderBytecode = createRecord.csBytecode.data();
	if (createRecord.device)
		createRecord.device->AddRef();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->AddRef();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->AddRef();
	shouldCreate = true;
	ReleaseSRWLockExclusive(&gModLock);

	if (!shouldCreate)
		return nullptr;

	ID3D12PipelineState *newReplacement = createRecord.kind == DX12PsoKind::Graphics ?
		CreateGraphicsReplacement(&createRecord) :
		CreateComputeReplacement(&createRecord);
	if (createRecord.device)
		createRecord.device->Release();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->Release();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->Release();
	if (!newReplacement)
		return nullptr;

	AcquireSRWLockExclusive(&gModLock);
	it = gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end() || gReloadGeneration != generation) {
		ReleaseSRWLockExclusive(&gModLock);
		newReplacement->Release();
		return nullptr;
	}
	if (it->second.replacement)
		it->second.replacement->Release();
	it->second.replacement = newReplacement;
	it->second.replacementGeneration = generation;
	ReleaseSRWLockExclusive(&gModLock);
	return newReplacement;
}


bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw)
{
	if (!commandList || !replacement || replacement->drawSrvPatches.empty())
		return false;
	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;
	const bool applied = DX12DrawSrvReplacementUtils::ApplyPending(
		device, commandList, replacement, executeOriginalDraw);
	device->Release();
	return applied;
}


void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement)
{
	DX12DrawSrvReplacementUtils::Restore(commandList, replacement);
}

void DX12ModClearIaReplacementRuntimeCaches()
{
	DX12IaReplacementExecutor::ClearPsoDrawFastRejectCache();
	ClearShaderOverrideNegativeCache();
	ClearCompiledPsoPlans();
}
// PreSkin DynamicUAV output resize map.
// Maps original game-created output UAV resources to resized buffers
// when the mod vertex count exceeds the original capacity.
// The IASetVertexBuffers hook reads this map to redirect vb0 reads.

class DX12PreSkinOutputResizeEntry
{
public:
	ID3D12Resource *resizedResource = nullptr;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedByteWidth = 0;
	UINT stride = 0;
	UINT64 resizedGpuVa = 0;
	bool uavBarrierPending = true;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

static std::unordered_map<UINT64, DX12PreSkinOutputResizeEntry>
	gPreSkinOutputResizeMap;
static SRWLOCK gPreSkinOutputResizeLock = SRWLOCK_INIT;
static std::atomic<UINT64> gPreSkinOutputResizeGeneration{1};

static void BumpPreSkinOutputResizeGeneration()
{
	UINT64 current = gPreSkinOutputResizeGeneration.load(std::memory_order_relaxed);
	for (;;) {
		UINT64 next = current == UINT64_MAX ? 1 : current + 1;
		if (gPreSkinOutputResizeGeneration.compare_exchange_weak(
				current,
				next,
				std::memory_order_release,
				std::memory_order_relaxed))
			return;
	}
}

void DX12ModRegisterPreSkinOutputResize(
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride)
{
	if (!originalResource || !resizedResource)
		return;
	const UINT64 originalGpuVa = originalResource->GetGPUVirtualAddress();
	const UINT64 resizedGpuVa = resizedResource->GetGPUVirtualAddress();
	if (!originalGpuVa || !resizedGpuVa)
		return;
	if (!originalByteWidth) {
		const D3D12_RESOURCE_DESC desc = originalResource->GetDesc();
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			originalByteWidth = desc.Width;
	}
	if (!originalByteWidth || !resizedByteWidth)
		return;
	resizedResource->AddRef();
	ID3D12Resource *oldResource = nullptr;
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	auto oldIt = gPreSkinOutputResizeMap.find(originalGpuVa);
	if (oldIt != gPreSkinOutputResizeMap.end())
		oldResource = oldIt->second.resizedResource;
	DX12PreSkinOutputResizeEntry entry;
	entry.resizedResource = resizedResource;
	entry.originalGpuVa = originalGpuVa;
	entry.originalByteWidth = originalByteWidth;
	entry.resizedByteWidth = resizedByteWidth;
	entry.stride = stride;
	entry.resizedGpuVa = resizedGpuVa;
	entry.uavBarrierPending = true;
	entry.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	gPreSkinOutputResizeMap[originalGpuVa] = entry;
	BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	if (oldResource)
		oldResource->Release();
}

bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding)
{
	if (!viewGpuVa || !binding)
		return false;
	binding->Clear();
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	DX12PreSkinOutputResizeEntry *entry = nullptr;
	UINT64 sourceOffset = 0;
	auto exactIt = gPreSkinOutputResizeMap.find(viewGpuVa);
	if (exactIt != gPreSkinOutputResizeMap.end()) {
		entry = &exactIt->second;
	} else {
		for (auto &item : gPreSkinOutputResizeMap) {
			DX12PreSkinOutputResizeEntry &candidate = item.second;
			if (!candidate.originalGpuVa || !candidate.originalByteWidth)
				continue;
			if (viewGpuVa < candidate.originalGpuVa)
				continue;
			const UINT64 candidateOffset = viewGpuVa - candidate.originalGpuVa;
			if (candidateOffset >= candidate.originalByteWidth)
				continue;
			entry = &candidate;
			sourceOffset = candidateOffset;
			break;
		}
	}
	if (!entry || !entry->resizedResource || sourceOffset >= entry->resizedByteWidth) {
		ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
		return false;
	}
	entry->resizedResource->AddRef();
	binding->resizedResource = entry->resizedResource;
	binding->retainedResource = true;
	binding->originalGpuVa = entry->originalGpuVa;
	binding->originalByteWidth = entry->originalByteWidth;
	binding->resizedGpuVa = entry->resizedGpuVa;
	binding->resizedByteWidth = entry->resizedByteWidth;
	binding->sourceOffset = sourceOffset;
	binding->stride = entry->stride;
	binding->uavBarrierPending = entry->uavBarrierPending;
	binding->transitionToVertex =
		entry->state != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	binding->stateBefore = entry->state;
	entry->uavBarrierPending = false;
	entry->state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	SetPreSkinOutputResizeCacheState(
		entry->resizedResource,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
	return true;
}

UINT64 DX12ModGetPreSkinOutputResizeGeneration()
{
	return gPreSkinOutputResizeGeneration.load(std::memory_order_acquire);
}

void DX12ModClearPreSkinOutputResizeMap()
{
	AcquireSRWLockExclusive(&gPreSkinOutputResizeLock);
	for (auto &item : gPreSkinOutputResizeMap) {
		if (item.second.resizedResource)
			item.second.resizedResource->Release();
		item.second.resizedResource = nullptr;
	}
	gPreSkinOutputResizeMap.clear();
	BumpPreSkinOutputResizeGeneration();
	ReleaseSRWLockExclusive(&gPreSkinOutputResizeLock);
}
