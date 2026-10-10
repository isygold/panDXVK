#include <algorithm>
#include <chrono>

#include "../util/util_env.h"
#include "../util/util_time.h"

#include "dxvk_graphics.h"
#include "dxvk_pipecompiler.h"

namespace dxvk {

  static std::string describePipeline(const DxvkGraphicsPipeline* pipeline) {
    const DxvkGraphicsPipelineShaders& shaders = pipeline->shaders();
    std::string result;

    auto append = [&] (const char* name, const Rc<DxvkShader>& shader) {
      if (shader == nullptr)
        return;

      if (!result.empty())
        result += ' ';

      result += str::format(name, "=", shader->debugName());
    };

    append("vs",  shaders.vs);
    append("tcs", shaders.tcs);
    append("tes", shaders.tes);
    append("gs",  shaders.gs);
    append("fs",  shaders.fs);
    return result;
  }


  DxvkPipelineCompiler::DxvkPipelineCompiler(uint32_t numThreads, bool enableLog) {
    m_log = enableLog;
    m_workers.reserve(numThreads);

    for (uint32_t i = 0; i < numThreads; i++)
      m_workers.emplace_back([this, i] { this->runWorker(i); });
  }


  DxvkPipelineCompiler::~DxvkPipelineCompiler() {
    this->stopWorkerThreads();
  }


  void DxvkPipelineCompiler::queueCompilation(
          DxvkGraphicsPipeline*           pipeline,
    const DxvkGraphicsPipelineStateInfo&  state,
    const DxvkRenderPass*                 renderPass) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    if (m_stop)
      return;

    for (auto& entry : m_entries) {
      if (entry.pipeline == pipeline
       && entry.renderPass == renderPass
       && entry.state == state) {
        entry.skippedDraws += 1;
        m_stats.repeated += 1;
        return;
      }
    }

    m_entries.push_back({ pipeline, renderPass, state, false, 1 });

    m_stats.queued += 1;
    m_stats.peakQueue = std::max(m_stats.peakQueue, uint32_t(m_entries.size()));

    if (m_log) {
      Logger::info(str::format("panDXVK async: queued ",
        describePipeline(pipeline), " (pending: ", m_entries.size(), ")"));
    }

    m_cond.notify_one();
  }


  bool DxvkPipelineCompiler::isBusy() const {
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    return !m_entries.empty();
  }


  void DxvkPipelineCompiler::stopWorkerThreads() {
    bool firstStop;

    {
      std::lock_guard<dxvk::mutex> lock(m_mutex);
      firstStop = !m_stop;
      m_stop = true;
      m_cond.notify_all();
    }

    for (auto& worker : m_workers) {
      if (worker.joinable())
        worker.join();
    }

    if (firstStop && m_log) {
      std::lock_guard<dxvk::mutex> lock(m_mutex);

      Logger::info(str::format("panDXVK async: stopped, queued=", m_stats.queued,
        " finished=", m_stats.finished, " failed=", m_stats.failed,
        " repeated requests=", m_stats.repeated,
        " peak pending=", m_stats.peakQueue,
        " total compile time=", m_stats.totalMs, " ms",
        " dropped at shutdown=", m_entries.size()));
    }
  }


  void DxvkPipelineCompiler::runWorker(uint32_t index) {
    env::setThreadName("dxvk-async");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (!m_stop) {
      auto entry = std::find_if(m_entries.begin(), m_entries.end(),
        [] (const Entry& e) { return !e.started; });

      if (entry == m_entries.end()) {
        m_cond.wait(lock);
        continue;
      }

      entry->started = true;
      lock.unlock();

      if (m_log) {
        Logger::info(str::format("panDXVK async: worker ", index,
          " compiling ", describePipeline(entry->pipeline)));
      }

      auto t0 = dxvk::high_resolution_clock::now();
      bool ok = entry->pipeline->compilePipelineAsync(entry->state, entry->renderPass);
      auto t1 = dxvk::high_resolution_clock::now();

      double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

      lock.lock();

      m_stats.totalMs += ms;

      if (ok)
        m_stats.finished += 1;
      else
        m_stats.failed += 1;

      if (m_log) {
        if (ok) {
          Logger::info(str::format("panDXVK async: finished ",
            describePipeline(entry->pipeline), " in ", ms, " ms, ",
            entry->skippedDraws, " draw calls skipped (pending: ",
            m_entries.size() - 1, ")"));
        } else {
          Logger::err(str::format("panDXVK async: FAILED ",
            describePipeline(entry->pipeline), " after ", ms,
            " ms, the driver returned no pipeline and every draw using it stays skipped, ",
            entry->skippedDraws, " draw calls skipped so far"));
        }
      }

      m_entries.erase(entry);
    }
  }

}
