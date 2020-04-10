#include <cg_base.hpp>

namespace cgb
{
	std::deque<device_queue> device_queue::sPreparedQueues;

	device_queue* device_queue::prepare(
		vk::QueueFlags aFlagsRequired,
		device_queue_selection_strategy aSelectionStrategy,
		std::optional<vk::SurfaceKHR> aSupportForSurface)
	{
		auto families = context().find_best_queue_family_for(aFlagsRequired, aSelectionStrategy, aSupportForSurface);
		if (families.size() == 0) {
			throw cgb::runtime_error("Couldn't find queue families satisfying the given criteria.");
		}

		// Default to the first ones, each
		uint32_t familyIndex = std::get<0>(families[0]);
		uint32_t queueIndex = 0;

		for (auto& family : families) {
			for (uint32_t qi = 0; qi < std::get<1>(family).queueCount; ++qi) {

				auto alreadyInUse = std::find_if(
					std::begin(sPreparedQueues), 
					std::end(sPreparedQueues), 
					[lFamilyIndexInQuestion = std::get<0>(family), lQueueIndexInQuestion = qi](const auto& pq) {
					return pq.family_index() == lFamilyIndexInQuestion
						&& pq.queue_index() == lQueueIndexInQuestion;
				});

				// Pay attention to different selection strategies:
				switch (aSelectionStrategy)
				{
				case cgb::device_queue_selection_strategy::prefer_separate_queues:
					if (sPreparedQueues.end() == alreadyInUse) {
						// didn't find combination, that's good
						familyIndex = std::get<0>(family);
						queueIndex = qi;
						goto found_indices;
					}
					break;
				case cgb::device_queue_selection_strategy::prefer_everything_on_single_queue:
					if (sPreparedQueues.end() != alreadyInUse) {
						// find combination, that's good in this case
						familyIndex = std::get<0>(family);
						queueIndex = 0; // => 0 ... i.e. everything on queue #0
						goto found_indices;
					}
					break;
				}
			}
		}

	found_indices:
		auto& prepd_queue = sPreparedQueues.emplace_back();
		prepd_queue.mQueueFamilyIndex = familyIndex;
		prepd_queue.mQueueIndex = queueIndex;
		prepd_queue.mPriority = 0.5f; // default priority of 0.5
		prepd_queue.mQueue = nullptr;
		return &prepd_queue;
	}

	device_queue device_queue::create(uint32_t aQueueFamilyIndex, uint32_t aQueueIndex)
	{
		device_queue result;
		result.mQueueFamilyIndex = aQueueFamilyIndex;
		result.mQueueIndex = aQueueIndex;
		result.mPriority = 0.5f; // default priority of 0.5f
		result.mQueue = context().logical_device().getQueue(result.mQueueFamilyIndex, result.mQueueIndex);
		return result;
	}

	device_queue device_queue::create(const device_queue& aPreparedQueue)
	{
		device_queue result;
		result.mQueueFamilyIndex = aPreparedQueue.family_index();
		result.mQueueIndex = aPreparedQueue.queue_index();
		result.mPriority = aPreparedQueue.mPriority; // default priority of 0.5f
		result.mQueue = context().logical_device().getQueue(result.mQueueFamilyIndex, result.mQueueIndex);
		return result;
	}

	command_pool& device_queue::pool_for(vk::CommandPoolCreateFlags aFlags) const
	{ 
		return context().get_command_pool_for_queue(*this, aFlags); 
	}

	command_buffer device_queue::create_command_buffer(bool aSimultaneousUseEnabled) const
	{
		auto flags = vk::CommandBufferUsageFlags();
		if (aSimultaneousUseEnabled) {
			flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		}
		auto result = pool_for(vk::CommandPoolCreateFlags{}) // no special flags
			.get_command_buffer(flags);
		return result;
	}

	std::vector<command_buffer> device_queue::create_command_buffers(uint32_t aNumBuffers, bool aSimultaneousUseEnabled) const
	{
		auto flags = vk::CommandBufferUsageFlags();
		if (aSimultaneousUseEnabled) {
			flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		}
		auto result = pool_for(vk::CommandPoolCreateFlags{}) // no special flags
			.get_command_buffers(aNumBuffers, flags);
		return result;
	}
	
	command_buffer device_queue::create_single_use_command_buffer() const
	{
		const vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		auto result = pool_for(vk::CommandPoolCreateFlagBits::eTransient)
			.get_command_buffer(flags);
		return result;
	}

	std::vector<command_buffer> device_queue::create_single_use_command_buffers(uint32_t aNumBuffers) const
	{
		const vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		auto result = pool_for(vk::CommandPoolCreateFlagBits::eTransient)
			.get_command_buffers(aNumBuffers, flags);
		return result;		
	}

	command_buffer device_queue::create_resettable_command_buffer(bool aSimultaneousUseEnabled) const
	{
		vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit | vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		if (aSimultaneousUseEnabled) {
			flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		}
		auto result = pool_for(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			.get_command_buffer(flags);
		return result;
	}

	std::vector<command_buffer> device_queue::create_resettable_command_buffers(uint32_t aNumBuffers, bool aSimultaneousUseEnabled) const
	{
		vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit | vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		if (aSimultaneousUseEnabled) {
			flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
		}
		auto result = pool_for(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			.get_command_buffers(aNumBuffers, flags);
		return result;
	}
	
	std::optional<fence> device_queue::submit(command_buffer_t& aCommandBuffer, bool aCreateFence)
	{
		std::optional<fence> fen = aCreateFence ? std::optional<fence>{ fence_t::create() } : std::optional<fence>{};
		
		assert(aCommandBuffer.state() >= command_buffer_state::finished_recording);
		const auto submitInfo = vk::SubmitInfo{}
			.setCommandBufferCount(1u)
			.setPCommandBuffers(aCommandBuffer.handle_addr());
		handle().submit({ submitInfo }, fen.has_value() ? fen.value()->handle() : nullptr);
		aCommandBuffer.mState = command_buffer_state::submitted;

		return fen;
	}
	// The code between these two ^ and v is mostly copied... I know. It avoids the usage of an unneccessary
	// temporary vector in single command buffer-case. Should, however, probably be refactored.
	std::optional<fence> device_queue::submit(std::vector<std::reference_wrapper<command_buffer_t>> aCommandBuffers, bool aCreateFence)
	{
		std::optional<fence> fen = aCreateFence ? std::optional<fence>{ fence_t::create() } : std::optional<fence>{};
		
		std::vector<vk::CommandBuffer> handles;
		handles.reserve(aCommandBuffers.size());
		for (auto& cb : aCommandBuffers) {
			assert(cb.get().state() >= command_buffer_state::finished_recording);
			handles.push_back(cb.get().handle());
		}
		
		const auto submitInfo = vk::SubmitInfo{}
			.setCommandBufferCount(static_cast<uint32_t>(handles.size()))
			.setPCommandBuffers(handles.data());
		handle().submit({ submitInfo }, fen.has_value() ? fen.value()->handle() : nullptr);

		for (auto& cb : aCommandBuffers) {
			cb.get().mState = command_buffer_state::submitted;
		}
		
		return fen;
	}
	
	semaphore device_queue::submit_and_handle_with_semaphore(command_buffer aCommandBuffer, std::vector<semaphore> aWaitSemaphores)
	{
		assert(aCommandBuffer->state() >= command_buffer_state::finished_recording);
		
		// Create a semaphore which can, or rather, MUST be used to wait for the results
		auto signalWhenCompleteSemaphore = semaphore_t::create();
		
		if (0 == aWaitSemaphores.size()) {
			// Optimized route for 0 _WaitSemaphores
			const auto submitInfo = vk::SubmitInfo{}
				.setCommandBufferCount(1u)
				.setPCommandBuffers(aCommandBuffer->handle_addr())
				.setWaitSemaphoreCount(0u)
				.setPWaitSemaphores(nullptr)
				.setPWaitDstStageMask(nullptr)
				.setSignalSemaphoreCount(1u)
				.setPSignalSemaphores(signalWhenCompleteSemaphore->handle_addr());

			handle().submit({ submitInfo }, nullptr);
			aCommandBuffer->mState = command_buffer_state::submitted;

			signalWhenCompleteSemaphore->set_custom_deleter([
				lOwnedCommandBuffer{ std::move(aCommandBuffer) } // Take care of the command_buffer's lifetime.. OMG!
			](){});
		}
		else {
			// Also set the wait semaphores and take care of their lifetimes
			std::vector<vk::Semaphore> waitSemaphoreHandles;
			waitSemaphoreHandles.reserve(aWaitSemaphores.size());
			std::vector<vk::PipelineStageFlags> waitDstStageMasks;
			waitDstStageMasks.reserve(aWaitSemaphores.size());
			
			for (const auto& semaphoreDependency : aWaitSemaphores) {
				waitSemaphoreHandles.push_back(semaphoreDependency->handle());
				waitDstStageMasks.push_back(semaphoreDependency->semaphore_wait_stage());
			}
			
			const auto submitInfo = vk::SubmitInfo{}
				.setCommandBufferCount(1u)
				.setPCommandBuffers(aCommandBuffer->handle_addr())
				.setWaitSemaphoreCount(static_cast<uint32_t>(waitSemaphoreHandles.size()))
				.setPWaitSemaphores(waitSemaphoreHandles.data())
				.setPWaitDstStageMask(waitDstStageMasks.data())
				.setSignalSemaphoreCount(1u)
				.setPSignalSemaphores(signalWhenCompleteSemaphore->handle_addr());

			handle().submit({ submitInfo }, nullptr);
			aCommandBuffer->mState = command_buffer_state::submitted;

			signalWhenCompleteSemaphore->set_custom_deleter([
				lOwnedWaitSemaphores{ std::move(aWaitSemaphores) },
				lOwnedCommandBuffer{ std::move(aCommandBuffer) } // Take care of the command_buffer's lifetime.. OMG!
			](){});	
		}
		
		return signalWhenCompleteSemaphore;
	}
	// The code between these two ^ and v is mostly copied... I know. It avoids the usage of an unneccessary
	// temporary vector in single command buffer-case. Should, however, probably be refactored.
	semaphore device_queue::submit_and_handle_with_semaphore(std::vector<command_buffer> aCommandBuffers, std::vector<semaphore> aWaitSemaphores)
	{
		std::vector<vk::CommandBuffer> handles;
		handles.reserve(aCommandBuffers.size());
		for (auto& cb : aCommandBuffers) {
			assert(cb->state() >= command_buffer_state::finished_recording);
			handles.push_back(cb->handle());
		}
		
		// Create a semaphore which can, or rather, MUST be used to wait for the results
		auto signalWhenCompleteSemaphore = semaphore_t::create();
		
		if (0 == aWaitSemaphores.size()) {
			// Optimized route for 0 _WaitSemaphores
			const auto submitInfo = vk::SubmitInfo{}
				.setCommandBufferCount(static_cast<uint32_t>(handles.size()))
				.setPCommandBuffers(handles.data())
				.setWaitSemaphoreCount(0u)
				.setPWaitSemaphores(nullptr)
				.setPWaitDstStageMask(nullptr)
				.setSignalSemaphoreCount(1u)
				.setPSignalSemaphores(signalWhenCompleteSemaphore->handle_addr());

			handle().submit({ submitInfo }, nullptr);

			for (auto& cb : aCommandBuffers) {
				cb->mState = command_buffer_state::submitted;
			}

			signalWhenCompleteSemaphore->set_custom_deleter([
				lOwnedCommandBuffer{ std::move(aCommandBuffers) } // Take care of the command_buffer's lifetime.. OMG!
			](){});
		}
		else {
			// Also set the wait semaphores and take care of their lifetimes
			std::vector<vk::Semaphore> waitSemaphoreHandles;
			waitSemaphoreHandles.reserve(aWaitSemaphores.size());
			std::vector<vk::PipelineStageFlags> waitDstStageMasks;
			waitDstStageMasks.reserve(aWaitSemaphores.size());
			
			for (const auto& semaphoreDependency : aWaitSemaphores) {
				waitSemaphoreHandles.push_back(semaphoreDependency->handle());
				waitDstStageMasks.push_back(semaphoreDependency->semaphore_wait_stage());
			}
			
			const auto submitInfo = vk::SubmitInfo{}
				.setCommandBufferCount(static_cast<uint32_t>(handles.size()))
				.setPCommandBuffers(handles.data())
				.setWaitSemaphoreCount(static_cast<uint32_t>(waitSemaphoreHandles.size()))
				.setPWaitSemaphores(waitSemaphoreHandles.data())
				.setPWaitDstStageMask(waitDstStageMasks.data())
				.setSignalSemaphoreCount(1u)
				.setPSignalSemaphores(signalWhenCompleteSemaphore->handle_addr());

			handle().submit({ submitInfo }, nullptr);
			
			for (auto& cb : aCommandBuffers) {
				cb->mState = command_buffer_state::submitted;
			}

			signalWhenCompleteSemaphore->set_custom_deleter([
				lOwnedWaitSemaphores{ std::move(aWaitSemaphores) },
				lOwnedCommandBuffer{ std::move(aCommandBuffers) } // Take care of the command_buffer's lifetime.. OMG!
			](){});	
		}
		
		return signalWhenCompleteSemaphore;
	}
}
