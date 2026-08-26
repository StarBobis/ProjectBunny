#pragma once

// DX12CommandListExecutor.h - part of DX12ModRuntimeInternal.h, included from it at the split position.
// Not meant to be included directly.


class DX12CommandListExecutorScratch
{
public:
	std::vector<std::wstring> commandListStack;
	std::unordered_set<std::wstring> commandListStackSet;
	std::vector<Bunny::CommandListTarget> descriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> targetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> graphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> resolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> graphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> shaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> targetTextureOverrideCache;

	void Reset()
	{
		commandListStack.clear();
		commandListStackSet.clear();
		descriptorTargetScratch.clear();
		targetMatchScratch.clear();
		graphicsSrvBindings.clear();
		resolvedGraphicsSrvBindings.clear();
		graphicsSrvBindingsByTarget.clear();
		graphicsSrvTextureOverrideCache.clear();
		shaderResourceOverrideBindingsByTarget.clear();
		targetTextureOverrideCache.clear();
	}
};


class DX12CommandListExecutor
{
public:
	inline static thread_local DX12CommandListExecutorScratch ScratchPool[18] = {};
	inline static thread_local UINT ScratchDepth = 0;

	static DX12CommandListExecutorScratch *AcquireScratch(
		bool *pooled,
		DX12CommandListExecutorScratch *fallback)
	{
		if (ScratchDepth < ARRAYSIZE(ScratchPool)) {
			DX12CommandListExecutorScratch *scratch = &ScratchPool[ScratchDepth++];
			scratch->Reset();
			*pooled = true;
			return scratch;
		}
		fallback->Reset();
		*pooled = false;
		return fallback;
	}

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

	class BoolScope
	{
	public:
		BoolScope(bool *flag, bool value)
			: target(flag),
			  saved(flag ? *flag : false)
		{
			if (target)
				*target = value;
		}

		~BoolScope()
		{
			if (target)
				*target = saved;
		}

	private:
		bool *target = nullptr;
		bool saved = false;
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
		  mPendingResourceViews(pendingResourceViews),
		  mScratch(AcquireScratch(&mScratchPooled, &mFallbackScratch)),
		  mCommandListStack(mScratch->commandListStack),
		  mCommandListStackSet(mScratch->commandListStackSet),
		  mDescriptorTargetScratch(mScratch->descriptorTargetScratch),
		  mTargetMatchScratch(mScratch->targetMatchScratch),
		  mGraphicsSrvBindings(mScratch->graphicsSrvBindings),
		  mResolvedGraphicsSrvBindings(mScratch->resolvedGraphicsSrvBindings),
		  mGraphicsSrvBindingsByTarget(mScratch->graphicsSrvBindingsByTarget),
		  mGraphicsSrvTextureOverrideCache(mScratch->graphicsSrvTextureOverrideCache),
		  mShaderResourceOverrideBindingsByTarget(mScratch->shaderResourceOverrideBindingsByTarget),
		  mTargetTextureOverrideCache(mScratch->targetTextureOverrideCache)
	{
		mCurrentIaState = iaState;
		mCommandListStack.reserve(8);
		mCommandListStackSet.reserve(8);
		mDescriptorTargetScratch.reserve(8);
		mTargetMatchScratch.reserve(8);
		mGraphicsSrvBindings.reserve(32);
		mResolvedGraphicsSrvBindings.reserve(32);
		mGraphicsSrvBindingsByTarget.reserve(16);
		mGraphicsSrvTextureOverrideCache.reserve(16);
		mShaderResourceOverrideBindingsByTarget.reserve(16);
		mTargetTextureOverrideCache.reserve(16);
	}

	~DX12CommandListExecutor()
	{
		mScratch->Reset();
		if (mScratchPooled)
			--ScratchDepth;
	}

	void SetExecutionMode(bool executeCommands, bool executeDrawActions)
	{
		mExecuteCommands = executeCommands;
		mExecuteDrawActions = executeDrawActions;
	}

	void SetSkipDirectDescriptorTargets(bool skip)
	{
		mSkipDirectDescriptorTargets = skip;
	}

	void AdoptDirectDescriptorPrefilterCache()
	{
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		if (!tlsDirectDescriptorPrefilterCache.MatchesInvocation(
				reloadGeneration,
				mCommandList,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance)) {
			return;
		}
		for (const auto &entry : tlsDirectDescriptorPrefilterCache.matchesByTarget)
			mGraphicsSrvTextureOverrideCache[entry.first] = entry.second;
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
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
		const std::shared_ptr<const DX12CompiledShaderOverridePlan> compiledPlan =
			GetCompiledShaderOverridePlan(config.section);
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
			GetCompiledTextureOverridePlan(*queued.config);
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
			GetCompiledShaderOverridePlan(config.section);
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
			action.kind == Bunny::CommandListActionKind::DrawIndexedAuto ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.fromCaller =
			action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
			action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller;
		draw.autoCount = action.kind == Bunny::CommandListActionKind::DrawIndexedAuto;
		if (!draw.fromCaller && !draw.autoCount) {
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
		if (!indexResource.empty() || !vertexResources.empty())
			InvalidateTargetCaches();
		ApplyIaStateResourceOverrides(indexResource, vertexResources);
		if (mPendingResourceViews) {
			DX12PendingResourceViewRequest request;
			request.indexResource = indexResource;
			request.vertexResources = vertexResources;
			mPendingResourceViews->push_back(std::move(request));
			return;
		}
		mChanged |= AppendResourceViewsForCommandList(
			mDevice, mCommandList, mCurrentIaState, indexResource, vertexResources, mReplacement);
	}

	void ApplyIaStateResourceOverrides(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		if (!mDevice)
			return;
		if (!indexResource.empty()) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, indexResource);
			if (resource && resource->resource) {
				const DXGI_FORMAT indexFormat =
					resource->format == DXGI_FORMAT_R16_UINT ||
					resource->format == DXGI_FORMAT_R32_UINT ?
						resource->format :
						(mCurrentIaState.hasIndexBuffer &&
						 mCurrentIaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
							mCurrentIaState.indexView.Format :
							DXGI_FORMAT_R32_UINT);
				mCurrentIaState.hasIndexBuffer = true;
				mCurrentIaState.indexView.BufferLocation =
					resource->resource->GetGPUVirtualAddress();
				mCurrentIaState.indexView.SizeInBytes = static_cast<UINT>(
					(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
				mCurrentIaState.indexView.Format = indexFormat;
				// Must use the same hash function as Hunting/Dump/runtime
				// matching (DX12HashIaBufferContentView, full-resource
				// semantics): the state produced here feeds
				// FindTargetTextureOverride, which compares against hashes
				// recorded from the hunting overlay / SSMT dumps.
				mCurrentIaState.indexHash = DX12HashIaBufferContentView(
					mCurrentIaState.indexView.BufferLocation,
					mCurrentIaState.indexView.SizeInBytes,
					0,
					static_cast<UINT>(indexFormat),
					0);
			}
		}

		for (const DX12CompiledVertexResourceBinding &item : vertexResources) {
			DX12LoadedResource *resource =
				EnsureLoadedResourceForPreSkin(mDevice, item.resource);
			if (!resource || !resource->resource || item.slot >= 32)
				continue;
			const UINT stride =
				resource->stride ? resource->stride :
				FindIaVertexSlot(mCurrentIaState, item.slot) ?
					FindIaVertexSlot(mCurrentIaState, item.slot)->vertexView.StrideInBytes :
					0;
			DX12IaBufferHash *existing = nullptr;
			for (DX12IaBufferHash &buffer : mCurrentIaState.vertexBuffers) {
				if (buffer.slot == item.slot) {
					existing = &buffer;
					break;
				}
			}
			DX12IaBufferHash created = {};
			if (!existing) {
				created.slot = item.slot;
				mCurrentIaState.vertexBuffers.push_back(created);
				existing = &mCurrentIaState.vertexBuffers.back();
			}
			existing->slot = item.slot;
			existing->vertexView.BufferLocation =
				resource->resource->GetGPUVirtualAddress();
			existing->vertexView.SizeInBytes = static_cast<UINT>(
				(std::min)(resource->byteWidth, static_cast<UINT64>(UINT_MAX)));
			existing->vertexView.StrideInBytes = stride;
			existing->hash = DX12HashIaBufferContentView(
				existing->vertexView.BufferLocation,
				existing->vertexView.SizeInBytes,
				existing->vertexView.StrideInBytes,
				0,
				item.slot);
		}
	}

	bool ApplySetDescriptorResourceAction(
		const Bunny::CommandListTarget &target,
		const std::wstring &resourceName)
	{
		if ((target.kind != Bunny::CommandListTargetKind::ShaderResource &&
		     target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
		     target.kind != Bunny::CommandListTargetKind::UnorderedAccessView) ||
		    resourceName.empty()) {
			return false;
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		const std::vector<DX12ResolvedGraphicsSrvBinding> *baseBindings =
			target.stage == Bunny::CommandListShaderStage::Compute ?
				nullptr :
				GetResolvedGraphicsSrvBindingsForTarget(target);
		std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
		if (!BuildResolvedDescriptorBindingsForResourceOverride(
				mDevice,
				mCommandList,
				target,
				resourceName,
				baseBindings,
				&resolvedBindings)) {
			return false;
		}
		mShaderResourceOverrideBindingsByTarget[cacheKey] =
			std::move(resolvedBindings);
		InvalidateGraphicsSrvBindingCaches();
		InvalidateTargetCaches();
		return true;
	}

	bool FindTargetTextureOverride(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto cached = mTargetTextureOverrideCache.find(cacheKey);
		if (cached != mTargetTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}

		const bool descriptorTarget =
			target.kind == Bunny::CommandListTargetKind::ConstantBuffer ||
			target.kind == Bunny::CommandListTargetKind::ShaderResource ||
			target.kind == Bunny::CommandListTargetKind::UnorderedAccessView;
		if (descriptorTarget) {
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			auto descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (!mGraphicsSrvTextureOverrideCacheResolved)
				BuildDescriptorTextureOverrideCache();
			descriptorCached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
			if (descriptorCached != mGraphicsSrvTextureOverrideCache.end()) {
				mTargetTextureOverrideCache.emplace(cacheKey, descriptorCached->second);
				if (matches)
					*matches = descriptorCached->second;
				return !descriptorCached->second.empty();
			}
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				mTargetTextureOverrideCache.emplace(
					cacheKey,
					std::vector<DX12TextureOverrideMatch>());
				return false;
			}
			if (mSkipDirectDescriptorTargetsActive)
				return false;
			observedDescriptorTarget = true;
			mTargetTextureOverrideCache.emplace(
				cacheKey,
				std::vector<DX12TextureOverrideMatch>());
			return false;
		}
		const bool found = ::FindTargetTextureOverride(
			mCommandList, mCurrentIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex, mFirstInstance, matches,
			nullptr);
		mTargetTextureOverrideCache.emplace(
			cacheKey,
			matches ? *matches : std::vector<DX12TextureOverrideMatch>());
		return found;
	}

	void RunTextureOverride(
		const DX12TextureOverrideMatch &match, int depth, bool includePost)
	{
		const Bunny::TextureOverrideConfig *config = match.config;
		if (!mReplacement || depth > 16 || !config)
			return;
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			false);

		const std::shared_ptr<const DX12CompiledTextureOverridePlan> compiledPlan =
			GetCompiledTextureOverridePlan(*config);
		const DX12CompiledTextureOverridePlan *compiledPlanView =
			compiledPlan.get();
		const bool compiledTextureOverride =
			compiledPlanView && !compiledPlanView->hasUnsupportedCommandActions;
		if (includePost) {
			if (!match.postRuntimeEffect)
				return;
			if (compiledTextureOverride) {
				if (compiledPlanView->hasPostCommandListPlan) {
					ExecuteCompiledCommandListPlan(
						compiledPlanView->postCommandListPlan, depth + 1, true);
				}
			} else {
				for (const std::wstring &list : config->commandLists.post) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, true, &match);
				}
			}
			return;
		}

		QueuePostTextureOverride(match);
		if (!match.preRuntimeEffect)
			return;

		if (compiledTextureOverride) {
			if (compiledPlanView->hasPreCommandListPlan)
				ExecuteCompiledCommandListPlan(
					compiledPlanView->preCommandListPlan, depth + 1, false);
			RunCompiledTextureOverridePlan(*compiledPlanView, depth, includePost);
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

			}
		}

		const std::vector<DX12CurrentShaderResourceBinding> *graphicsSrvBindings =
			GetGraphicsSrvBindings();
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
		BoolScope directDescriptorSkipScope(
			&mSkipDirectDescriptorTargetsActive,
			mSkipDirectDescriptorTargets);
		if (mReplacement) {
			for (const std::wstring &list : plan.postCommandLists)
				mReplacement->AddPostCommandList(list);
		}
		ExecuteCompiledCommandListPlan(plan.mainPlan, 0, false);
	}

	void CollectDescriptorCheckTargetsFromCompiledPlans(
		const std::vector<DX12CompiledShaderOverridePlan> &plans)
	{
		mDescriptorTargetScratch.clear();
		for (const DX12CompiledShaderOverridePlan &plan : plans) {
			if (!plan.mainPlan.hasDescriptorCheckTargets)
				continue;
			for (const Bunny::CommandListTarget &target :
				 plan.mainPlan.descriptorCheckTargets) {
				bool exists = false;
				for (const Bunny::CommandListTarget &existing :
					 mDescriptorTargetScratch) {
					if (existing.kind == target.kind &&
						existing.stage == target.stage &&
						existing.slot == target.slot) {
						exists = true;
						break;
					}
				}
				if (!exists)
					mDescriptorTargetScratch.push_back(target);
			}
		}
	}

	void RunCompiledShaderOverridePostPlan(const DX12CompiledShaderOverridePlan &plan)
	{
		if (plan.unsupported || !plan.hasPostCommandListPlan)
			return;
		ExecuteCompiledCommandListPlan(plan.postCommandListPlan, 0, true);
	}

	void BuildDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mDescriptorTextureOverrideCachePrebuilt = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount))
			return;

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendTarget = [this, &snapshot](const Bunny::CommandListTarget &target) {
			const DX12TextureOverrideTargetCacheKey cacheKey = {
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot
			};
			auto overridden = mShaderResourceOverrideBindingsByTarget.find(cacheKey);
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindingsView = nullptr;
			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			if (overridden != mShaderResourceOverrideBindingsByTarget.end()) {
				resolvedBindingsView = &overridden->second;
			} else if (target.kind == Bunny::CommandListTargetKind::ShaderResource &&
				(target.stage == Bunny::CommandListShaderStage::Vertex ||
				 target.stage == Bunny::CommandListShaderStage::Pixel)) {
				resolvedBindingsView = GetResolvedGraphicsSrvBindingsForTarget(target);
			} else {
				std::vector<DX12CurrentShaderResourceBinding> bindings;
				if (!DX12BindingTracker::GetCurrentShaderResourceBindingsByRegister(
						mCommandList,
						target.stage == Bunny::CommandListShaderStage::Compute,
						DescriptorRangeTypeForTarget(target.kind),
						target.slot,
						0,
						&bindings) || bindings.empty()) {
					return;
				}
				ResolveGraphicsSrvBindings(
					mCommandList,
					mDevice,
					bindings,
					&resolvedBindings);
				resolvedBindingsView = &resolvedBindings;
			}
			if (!resolvedBindingsView || resolvedBindingsView->empty())
				return;

			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			FindDescriptorTextureOverridesFromResolvedBindings(
				snapshot,
				target,
				*resolvedBindingsView,
				mVertexCount,
				mIndexCount,
				mInstanceCount,
				mFirstVertex,
				mFirstIndex,
				mFirstInstance,
				&resolvedMatches);
			if (!resolvedMatches.empty())
				mGraphicsSrvTextureOverrideCache[cacheKey] = std::move(resolvedMatches);
		};

		if (targets) {
			if (targets->empty())
				return;
			for (const Bunny::CommandListTarget &target : *targets)
				appendTarget(target);
			return;
		}

		BuildDescriptorTextureOverrideCache();
	}

	void PrimeDescriptorTextureOverrideCacheForTargets(
		const std::vector<Bunny::CommandListTarget> *targets)
	{
		if (mDescriptorTextureOverrideCachePrebuilt)
			return;
		BuildDescriptorTextureOverrideCacheForTargets(targets);
	}

	void CollectDescriptorCheckTargetsFromActions(
		const std::vector<Bunny::CommandListAction> &actions)
	{
		mDescriptorTargetScratch.clear();
		for (const Bunny::CommandListAction &action : actions) {
			if (action.kind != Bunny::CommandListActionKind::CheckTextureOverride)
				continue;
			if (action.target.kind != Bunny::CommandListTargetKind::ConstantBuffer &&
				action.target.kind != Bunny::CommandListTargetKind::ShaderResource &&
				action.target.kind != Bunny::CommandListTargetKind::UnorderedAccessView)
				continue;
			bool exists = false;
			for (const Bunny::CommandListTarget &existing : mDescriptorTargetScratch) {
				if (existing.kind == action.target.kind &&
					existing.stage == action.target.stage &&
					existing.slot == action.target.slot) {
					exists = true;
					break;
				}
			}
			if (!exists)
				mDescriptorTargetScratch.push_back(action.target);
		}
	}

	void RunCheckTextureOverrideTarget(
		const Bunny::CommandListTarget &target,
		int depth,
		bool includePost)
	{
		if (!HasTextureOverrideRuntimeEffect(includePost))
			return;
		mTargetMatchScratch.clear();
		if (!FindTargetTextureOverride(target, &mTargetMatchScratch))
			return;
		for (const DX12TextureOverrideMatch &match : mTargetMatchScratch)
			RunTextureOverride(match, depth + 1, includePost);
	}

	bool RunCompiledCommandListPlan(
		const std::wstring &name, int depth, bool includePost)
	{
		if (!mReplacement)
			return false;
		if (!HasCommandListRuntimeEffect(name, includePost))
			return false;
		if (!EnterCommandList(name))
			return true;
		const std::shared_ptr<const DX12CompiledCommandListPlan> plan =
			GetCompiledCommandListPlan(name);
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
		if (plan.hasDescriptorCheckTargets && !plan.descriptorCheckTargets.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&plan.descriptorCheckTargets);
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
				case DX12CompiledCommandListPlan::Op::Kind::SetShaderResource:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetConstantBuffer:
					ApplySetDescriptorResourceAction(op.target, op.resource);
					break;
				case DX12CompiledCommandListPlan::Op::Kind::SetUnorderedAccessView:
					ApplySetDescriptorResourceAction(op.target, op.resource);
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
					RunCheckTextureOverrideTarget(op.target, depth, includePost);
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
			// DX11 draw-time matching consumes the current bound resources, not a
			// stripped patch layout. TextureOverride matching and draw SRV patching
			// therefore share the same fully resolved current SRV snapshot.
			DX12BindingTracker::GetCurrentShaderResourceBindings(
				mCommandList,
				false,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				&mGraphicsSrvBindings);
			for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
				const DX12TextureOverrideTargetCacheKey &key = overrideEntry.first;
				if (key.kind != static_cast<uint32_t>(Bunny::CommandListTargetKind::ShaderResource) ||
				    key.stage == static_cast<uint32_t>(Bunny::CommandListShaderStage::Compute)) {
					continue;
				}
				Bunny::CommandListTarget target;
				target.kind = Bunny::CommandListTargetKind::ShaderResource;
				target.stage = static_cast<Bunny::CommandListShaderStage>(key.stage);
				target.slot = key.slot;
				mGraphicsSrvBindings.erase(
					std::remove_if(
						mGraphicsSrvBindings.begin(),
						mGraphicsSrvBindings.end(),
						[&target](const DX12CurrentShaderResourceBinding &binding) {
							return DescriptorBindingMatchesTarget(binding, target);
						}),
					mGraphicsSrvBindings.end());
				for (const DX12ResolvedGraphicsSrvBinding &resolved : overrideEntry.second)
					mGraphicsSrvBindings.push_back(resolved.binding);
			}
		}
		return &mGraphicsSrvBindings;
	}

	const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &
	GetTextureOverrideRuntimeSnapshot()
	{
		if (!mTextureOverrideRuntimeSnapshotResolved) {
			mTextureOverrideRuntimeSnapshotResolved = true;
			mTextureOverrideRuntimeSnapshot = LoadTextureOverrideRuntimeSnapshot();
		}
		return mTextureOverrideRuntimeSnapshot;
	}

	std::shared_ptr<const DX12CompiledTextureOverridePlan>
	GetCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledTextureOverridePlans.find(config.section);
		return found == snapshot->compiledTextureOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledShaderOverridePlan>
	GetCompiledShaderOverridePlan(const std::wstring &section)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledShaderOverridePlans.find(section);
		return found == snapshot->compiledShaderOverridePlans.end() ?
			nullptr : found->second;
	}

	std::shared_ptr<const DX12CompiledCommandListPlan>
	GetCompiledCommandListPlan(const std::wstring &name)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return nullptr;
		auto found = snapshot->compiledCommandListPlans.find(name);
		return found == snapshot->compiledCommandListPlans.end() ?
			nullptr : found->second;
	}

	bool HasTextureOverrideRuntimeEffect(bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || (includePost ?
			snapshot->anyTextureOverridePostRuntimeEffect :
			snapshot->anyTextureOverridePreRuntimeEffect);
	}

	bool HasCommandListRuntimeEffect(const std::wstring &name, bool includePost)
	{
		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		return !snapshot || CommandListRuntimeEffectLocked(snapshot, name, includePost);
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *GetResolvedGraphicsSrvBindings()
	{
		if (!mResolvedGraphicsSrvBindingsResolved) {
			mResolvedGraphicsSrvBindingsResolved = true;
			DX12SimulateContext *runtimeState =
				mCommandList ?
					DX12CommandListRuntime::GetStatePtrFast(mCommandList) :
					nullptr;
			const UINT64 descriptorQuerySerial =
				(runtimeState && runtimeState->graphicsDescriptorQuerySerial) ?
					runtimeState->graphicsDescriptorQuerySerial : 1;
			if (!TryLoadResolvedGraphicsSrvBindings(
					runtimeState,
					descriptorQuerySerial,
					&mResolvedGraphicsSrvBindings)) {
				const std::vector<DX12CurrentShaderResourceBinding> *bindings =
					GetGraphicsSrvBindings();
				if (bindings) {
					ResolveGraphicsSrvBindings(
						mCommandList,
						mDevice,
						*bindings,
						&mResolvedGraphicsSrvBindings);
					StoreResolvedGraphicsSrvBindings(
						runtimeState,
						descriptorQuerySerial,
						mResolvedGraphicsSrvBindings);
				}
			}
		}
		return &mResolvedGraphicsSrvBindings;
	}

	const std::vector<DX12ResolvedGraphicsSrvBinding> *
	GetResolvedGraphicsSrvBindingsForTarget(const Bunny::CommandListTarget &target)
	{
		if (target.kind != Bunny::CommandListTargetKind::ShaderResource ||
		    (target.stage != Bunny::CommandListShaderStage::Vertex &&
		     target.stage != Bunny::CommandListShaderStage::Pixel)) {
			return nullptr;
		}
		if (!mGraphicsSrvBindingsByTargetResolved) {
			mGraphicsSrvBindingsByTargetResolved = true;
			const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
				GetResolvedGraphicsSrvBindings();
			if (resolvedBindings) {
				for (const DX12ResolvedGraphicsSrvBinding &resolved :
				     *resolvedBindings) {
					if (resolved.binding.registerSpace != 0)
						continue;
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_VERTEX ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Vertex),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
					if (resolved.binding.shaderVisibility ==
					    D3D12_SHADER_VISIBILITY_PIXEL ||
					    resolved.binding.shaderVisibility ==
					        D3D12_SHADER_VISIBILITY_ALL) {
						mGraphicsSrvBindingsByTarget[DX12TextureOverrideTargetCacheKey{
							static_cast<uint32_t>(
								Bunny::CommandListTargetKind::ShaderResource),
							static_cast<uint32_t>(
								Bunny::CommandListShaderStage::Pixel),
							resolved.binding.shaderRegister
						}].push_back(resolved);
					}
				}
			}
		}
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		auto found = mGraphicsSrvBindingsByTarget.find(cacheKey);
		return found == mGraphicsSrvBindingsByTarget.end() ?
			nullptr : &found->second;
	}

	void BuildDescriptorTextureOverrideCache()
	{
		mGraphicsSrvTextureOverrideCacheResolved = true;
		mGraphicsSrvTextureOverrideCache.clear();
		if (!mCommandList)
			return;

		const std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot> &snapshot =
			GetTextureOverrideRuntimeSnapshot();
		if (!snapshot)
			return;
		if (!HasPotentialResolvedDescriptorPrefilterCandidates(
				*snapshot,
				mVertexCount,
				mIndexCount)) {
			return;
		}

		static thread_local std::vector<uint32_t> tlsAllowedSectionIds;
		CollectResolvedDescriptorPrefilterSectionIds(
			*snapshot,
			mVertexCount,
			mIndexCount,
			&tlsAllowedSectionIds);
		if (tlsAllowedSectionIds.empty())
			return;

		auto appendCurrentBindings = [this, &snapshot](
			D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			Bunny::CommandListTargetKind kind,
			bool compute) {
			if (kind == Bunny::CommandListTargetKind::ShaderResource && !compute) {
				const std::vector<DX12ResolvedGraphicsSrvBinding> *resolvedBindings =
					GetResolvedGraphicsSrvBindings();
				if (resolvedBindings && !resolvedBindings->empty()) {
					AppendResolvedBindingsToDescriptorTextureOverrideCache(
						snapshot,
						*resolvedBindings,
						kind,
						false,
						tlsAllowedSectionIds,
						&mGraphicsSrvTextureOverrideCache);
				}
				return;
			}

			std::vector<DX12CurrentShaderResourceBinding> bindings;
			if (!DX12BindingTracker::GetCurrentShaderResourceBindings(
					mCommandList,
					compute,
					rangeType,
					&bindings) || bindings.empty()) {
				return;
			}

			std::vector<DX12ResolvedGraphicsSrvBinding> resolvedBindings;
			ResolveGraphicsSrvBindings(
				mCommandList,
				mDevice,
				bindings,
				&resolvedBindings);
			if (resolvedBindings.empty())
				return;
			AppendResolvedBindingsToDescriptorTextureOverrideCache(
				snapshot,
				resolvedBindings,
				kind,
				compute,
				tlsAllowedSectionIds,
				&mGraphicsSrvTextureOverrideCache);
		};

		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			false);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
			Bunny::CommandListTargetKind::ShaderResource,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
			Bunny::CommandListTargetKind::ConstantBuffer,
			true);
		appendCurrentBindings(
			D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			Bunny::CommandListTargetKind::UnorderedAccessView,
			true);

		for (const auto &overrideEntry : mShaderResourceOverrideBindingsByTarget) {
			Bunny::CommandListTarget target;
			target.kind = static_cast<Bunny::CommandListTargetKind>(overrideEntry.first.kind);
			target.stage = static_cast<Bunny::CommandListShaderStage>(overrideEntry.first.stage);
			target.slot = overrideEntry.first.slot;
			std::vector<DX12TextureOverrideMatch> resolvedMatches;
			if (!overrideEntry.second.empty()) {
				FindDescriptorTextureOverridesFromResolvedBindings(
					snapshot,
					target,
					overrideEntry.second,
					mVertexCount,
					mIndexCount,
					mInstanceCount,
					mFirstVertex,
					mFirstIndex,
					mFirstInstance,
					&resolvedMatches);
			}
			if (resolvedMatches.empty()) {
				mGraphicsSrvTextureOverrideCache.erase(overrideEntry.first);
				continue;
			}
			mGraphicsSrvTextureOverrideCache[overrideEntry.first] =
				std::move(resolvedMatches);
		}
	}

	bool FindGraphicsSrvTextureOverrides(
		const Bunny::CommandListTarget &target,
		std::vector<DX12TextureOverrideMatch> *matches)
	{
		if (matches)
			matches->clear();
		const DX12TextureOverrideTargetCacheKey cacheKey = {
			static_cast<uint32_t>(target.kind),
			static_cast<uint32_t>(target.stage),
			target.slot
		};
		if (!mGraphicsSrvTextureOverrideCacheResolved) {
			BuildDescriptorTextureOverrideCache();
		}
		auto cached = mGraphicsSrvTextureOverrideCache.find(cacheKey);
		if (cached != mGraphicsSrvTextureOverrideCache.end()) {
			if (matches)
				*matches = cached->second;
			return !cached->second.empty();
		}
		return false;
	}

	void InvalidateTargetCaches()
	{
		mDescriptorTextureOverrideCachePrebuilt = false;
		mGraphicsSrvTextureOverrideCacheResolved = false;
		mGraphicsSrvTextureOverrideCache.clear();
		mTargetTextureOverrideCache.clear();
	}

	void InvalidateGraphicsSrvBindingCaches()
	{
		mGraphicsSrvBindingsResolved = false;
		mResolvedGraphicsSrvBindingsResolved = false;
		mGraphicsSrvBindingsByTargetResolved = false;
		mGraphicsSrvBindings.clear();
		mResolvedGraphicsSrvBindings.clear();
		mGraphicsSrvBindingsByTarget.clear();
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
		CollectDescriptorCheckTargetsFromActions(commandList.actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
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
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
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
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawIndexedAuto:
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
		CollectDescriptorCheckTargetsFromActions(actions);
		if (!mDescriptorTargetScratch.empty())
			PrimeDescriptorTextureOverrideCacheForTargets(&mDescriptorTargetScratch);
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
				RunCheckTextureOverrideTarget(action.target, depth, includePost);
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
			case Bunny::CommandListActionKind::SetShaderResource:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetConstantBuffer:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::SetUnorderedAccessView:
				ApplySetDescriptorResourceAction(action.target, action.resource);
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawIndexedAuto:
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
			if (!HasCommandListRuntimeEffect(list, false))
				continue;
			if (RunCompiledCommandListPlan(list, depth + 1, false))
				continue;
			const std::shared_ptr<const Bunny::CommandListConfig> config =
				SnapshotCommandListConfig(list);
			if (config)
				RunCommandList(*config, depth + 1, false, thisMatch);
		}
		for (const std::wstring &list : links.main) {
			if (!HasCommandListRuntimeEffect(list, includePost))
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
				if (!HasCommandListRuntimeEffect(list, true))
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
		std::pair<std::unordered_set<std::wstring>::iterator, bool> inserted =
			mCommandListStackSet.insert(name);
		if (inserted.second) {
			mCommandListStack.push_back(name);
			return true;
		}
		return false;
	}

	void LeaveCommandList()
	{
		if (!mCommandListStack.empty()) {
			mCommandListStackSet.erase(mCommandListStack.back());
			mCommandListStack.pop_back();
		}
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12GraphicsCommandList *mCommandList = nullptr;
	const DX12IaHashState &mIaState;
	DX12IaHashState mCurrentIaState;
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
	bool mSkipDirectDescriptorTargets = false;
	bool mSkipDirectDescriptorTargetsActive = false;
	bool mDescriptorTextureOverrideCachePrebuilt = false;
	bool mGraphicsSrvBindingsResolved = false;
	bool mResolvedGraphicsSrvBindingsResolved = false;
	bool mGraphicsSrvBindingsByTargetResolved = false;
	bool mGraphicsSrvTextureOverrideCacheResolved = false;
	bool mTextureOverrideRuntimeSnapshotResolved = false;
	DX12CommandListExecutorScratch mFallbackScratch;
	bool mScratchPooled = false;
	DX12CommandListExecutorScratch *mScratch = nullptr;
	std::vector<std::wstring> &mCommandListStack;
	std::unordered_set<std::wstring> &mCommandListStackSet;
	std::vector<Bunny::CommandListTarget> &mDescriptorTargetScratch;
	std::vector<DX12TextureOverrideMatch> &mTargetMatchScratch;
	std::vector<DX12CurrentShaderResourceBinding> &mGraphicsSrvBindings;
	std::vector<DX12ResolvedGraphicsSrvBinding> &mResolvedGraphicsSrvBindings;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mGraphicsSrvTextureOverrideCache;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12ResolvedGraphicsSrvBinding>,
		DX12TextureOverrideTargetCacheKeyHash> &mShaderResourceOverrideBindingsByTarget;
	std::unordered_map<
		DX12TextureOverrideTargetCacheKey,
		std::vector<DX12TextureOverrideMatch>,
		DX12TextureOverrideTargetCacheKeyHash> &mTargetTextureOverrideCache;
	std::shared_ptr<const DX12CommandExecutionRuntimeSnapshot>
		mTextureOverrideRuntimeSnapshot;
	MigotoCommandVmUtils::State mVm;
};
