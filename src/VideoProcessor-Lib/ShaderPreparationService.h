#pragma once

#include <ShaderPreparationCoordinator.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// A renderer-owned preparation worker. This service owns no libplacebo or D3D
// objects itself; the renderer supplies work that creates and destroys those
// objects exclusively on this worker. That keeps UI, presentation, and worker
// ownership explicit and prevents a future caller from accidentally dispatching
// `pl_render_image` against the live renderer with std::async.
namespace ShaderPreparationService
{
	using Generation = ShaderPreparationCoordinator::Generation;
	using Request = ShaderPreparationCoordinator::Request;
	using EnqueueResult = ShaderPreparationCoordinator::EnqueueResult;
	using CompletionResult = ShaderPreparationCoordinator::CompletionResult;

	class Cancellation
	{
	public:
		bool Requested() const
		{
			return m_control->retiring.load(std::memory_order_acquire) ||
				m_control->latestSerial.load(std::memory_order_acquire) != m_serial;
		}

	private:
		struct Control
		{
			std::atomic<uint64_t> latestSerial{ 0 };
			std::atomic_bool retiring{ false };
		};

		Cancellation(std::shared_ptr<Control> control, uint64_t serial) :
			m_control(std::move(control)), m_serial(serial) {}

		std::shared_ptr<Control> m_control;
		uint64_t m_serial = 0;
		friend class Service;
	};

	using Work = std::function<bool(const Request&, const Cancellation&)>;

	struct Completion
	{
		Request request;
		CompletionResult result = CompletionResult::DiscardUnknown;
		bool prepared = false;
		double durationMs = 0.0;
		DWORD workerThreadId = 0;
	};

	class Service
	{
	public:
		Service() : m_control(std::make_shared<Cancellation::Control>()),
			m_worker(&Service::Run, this) {}

		~Service()
		{
			Retire();
			Join();
		}

		Service(const Service&) = delete;
		Service& operator=(const Service&) = delete;

		EnqueueResult Enqueue(const Generation& generation, Work work,
			Request& assigned)
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			const EnqueueResult result = m_queue.Enqueue(generation, assigned);
			if (result == EnqueueResult::RejectedRetired)
				return result;

			m_control->latestSerial.store(m_queue.LatestRequest().serial,
				std::memory_order_release);
			if (result == EnqueueResult::Start)
			{
				m_activeRequest = assigned;
				m_activeWork = std::move(work);
				m_workAvailable.notify_one();
			}
			else if (result == EnqueueResult::ReplacedPending)
			{
				m_pendingWork = std::move(work);
			}
			return result;
		}

		void Retire() noexcept
		{
			{
				std::lock_guard<std::mutex> guard(m_mutex);
				if (m_retiring)
					return;
				m_retiring = true;
				m_queue.Retire();
				m_pendingWork = {};
				m_control->retiring.store(true, std::memory_order_release);
			}
			m_workAvailable.notify_all();
		}

		void Join() noexcept
		{
			if (m_worker.joinable())
				m_worker.join();
		}

		bool TryTakeCompletion(Completion& completion)
		{
			std::lock_guard<std::mutex> guard(m_mutex);
			if (m_completions.empty())
				return false;
			completion = m_completions.front();
			m_completions.erase(m_completions.begin());
			return true;
		}

		DWORD WorkerThreadId() const
		{
			return m_workerThreadId.load(std::memory_order_acquire);
		}

	private:
		void Run()
		{
			const DWORD workerThreadId = GetCurrentThreadId();
			m_workerThreadId.store(workerThreadId, std::memory_order_release);
			for (;;)
			{
				Request request;
				Work work;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_workAvailable.wait(lock, [this]()
						{
							return m_retiring || static_cast<bool>(m_activeWork);
						});
					if (m_retiring && !m_activeWork)
						break;
					request = m_activeRequest;
					work = std::move(m_activeWork);
				}

				const auto started = std::chrono::steady_clock::now();
				bool prepared = false;
				try
				{
					prepared = work && work(request,
						Cancellation(m_control, request.serial));
				}
				catch (...)
				{
					prepared = false;
				}
				const double durationMs = std::chrono::duration<double,
					std::milli>(std::chrono::steady_clock::now() - started).count();

				std::lock_guard<std::mutex> guard(m_mutex);
				Request next;
				const CompletionResult result = m_queue.Complete(request, next);
				m_completions.push_back(
					{ request, result, prepared, durationMs, workerThreadId });
				m_activeRequest = next;
				if (next.serial != 0 && !m_retiring)
				{
					m_activeWork = std::move(m_pendingWork);
					m_workAvailable.notify_one();
				}
				else
				{
					m_activeWork = {};
					m_pendingWork = {};
				}
			}
		}

		mutable std::mutex m_mutex;
		std::condition_variable m_workAvailable;
		ShaderPreparationCoordinator::Queue m_queue;
		std::shared_ptr<Cancellation::Control> m_control;
		Work m_activeWork;
		Work m_pendingWork;
		Request m_activeRequest;
		std::vector<Completion> m_completions;
		std::thread m_worker;
		std::atomic<DWORD> m_workerThreadId{ 0 };
		bool m_retiring = false;
	};
}
