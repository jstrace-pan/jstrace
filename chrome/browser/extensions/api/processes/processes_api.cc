// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/processes/processes_api.h"

#include <stdint.h>

#include <algorithm>

#include "base/lazy_instance.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/api/tabs/tabs_constants.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/task_manager/task_manager_interface.h"
#include "chrome/common/extensions/api/processes.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/result_codes.h"
#include "extensions/common/error_utils.h"
#include "third_party/WebKit/public/web/WebCache.h"

namespace extensions {

namespace errors {
const char kNotAllowedToTerminate[] = "Not allowed to terminate process: *.";
const char kProcessNotFound[] = "Process not found: *.";
const char kInvalidArgument[] = "Invalid argument: *.";
}  // namespace errors

namespace {

base::LazyInstance<BrowserContextKeyedAPIFactory<ProcessesAPI>>
    g_processes_api_factory = LAZY_INSTANCE_INITIALIZER;

int64_t GetRefreshTypesFlagOnlyEssentialData() {
  // This is the only non-optional data in the Process as defined by the API in
  // processes.idl.
  return task_manager::REFRESH_TYPE_NACL;
}

// This does not include memory. The memory refresh flag will only be added once
// a listener to OnUpdatedWithMemory event is added.
int64_t GetRefreshTypesForProcessOptionalData() {
  return task_manager::REFRESH_TYPE_CPU |
      task_manager::REFRESH_TYPE_NETWORK_USAGE |
      task_manager::REFRESH_TYPE_SQLITE_MEMORY |
      task_manager::REFRESH_TYPE_V8_MEMORY |
      task_manager::REFRESH_TYPE_WEBCACHE_STATS;
}

std::unique_ptr<api::processes::Cache> CreateCacheData(
    const blink::WebCache::ResourceTypeStat& stat) {
  std::unique_ptr<api::processes::Cache> cache(new api::processes::Cache());
  cache->size = static_cast<double>(stat.size);
  cache->live_size = static_cast<double>(stat.liveSize);
  return cache;
}

api::processes::ProcessType GetProcessType(
    task_manager::Task::Type task_type) {
  switch (task_type) {
    case task_manager::Task::BROWSER:
      return api::processes::PROCESS_TYPE_BROWSER;

    case task_manager::Task::RENDERER:
      return api::processes::PROCESS_TYPE_RENDERER;

    case task_manager::Task::EXTENSION:
    case task_manager::Task::GUEST:
      return api::processes::PROCESS_TYPE_EXTENSION;

    case task_manager::Task::PLUGIN:
      return api::processes::PROCESS_TYPE_PLUGIN;

    case task_manager::Task::WORKER:
      return api::processes::PROCESS_TYPE_WORKER;

    case task_manager::Task::NACL:
      return api::processes::PROCESS_TYPE_NACL;

    case task_manager::Task::UTILITY:
      return api::processes::PROCESS_TYPE_UTILITY;

    case task_manager::Task::GPU:
      return api::processes::PROCESS_TYPE_GPU;

    case task_manager::Task::UNKNOWN:
    case task_manager::Task::ARC:
    case task_manager::Task::SANDBOX_HELPER:
    case task_manager::Task::ZYGOTE:
      return api::processes::PROCESS_TYPE_OTHER;
  }

  NOTREACHED() << "Unknown task type.";
  return api::processes::PROCESS_TYPE_NONE;
}

// Fills |out_process| with the data of the process in which the task with |id|
// is running. If |include_optional| is true, this function will fill the
// optional fields in |api::processes::Process| except for |private_memory|,
// which should be filled later if needed.
void FillProcessData(
    task_manager::TaskId id,
    task_manager::TaskManagerInterface* task_manager,
    bool include_optional,
    api::processes::Process* out_process) {
  DCHECK(out_process);

  out_process->id = task_manager->GetChildProcessUniqueId(id);
  out_process->os_process_id = task_manager->GetProcessId(id);
  out_process->type = GetProcessType(task_manager->GetType(id));
  out_process->profile = base::UTF16ToUTF8(task_manager->GetProfileName(id));
  out_process->nacl_debug_port = task_manager->GetNaClDebugStubPort(id);

  // Collect the tab IDs of all the tasks sharing this renderer if any.
  const task_manager::TaskIdList tasks_on_process =
      task_manager->GetIdsOfTasksSharingSameProcess(id);
  for (const auto& task_id : tasks_on_process) {
    api::processes::TaskInfo task_info;
    task_info.title = base::UTF16ToUTF8(task_manager->GetTitle(task_id));
    const int tab_id = task_manager->GetTabId(task_id);
    if (tab_id != -1)
      task_info.tab_id.reset(new int(tab_id));

    out_process->tasks.push_back(std::move(task_info));
  }

  // If we don't need to include the optional properties, just return now.
  if (!include_optional)
    return;

  out_process->cpu.reset(new double(task_manager->GetCpuUsage(id)));

  out_process->network.reset(new double(static_cast<double>(
      task_manager->GetProcessTotalNetworkUsage(id))));

  int64_t v8_allocated = 0;
  int64_t v8_used = 0;
  if (task_manager->GetV8Memory(id, &v8_allocated, &v8_used)) {
    out_process->js_memory_allocated.reset(new double(static_cast<double>(
        v8_allocated)));
    out_process->js_memory_used.reset(new double(static_cast<double>(v8_used)));
  }

  const int64_t sqlite_bytes = task_manager->GetSqliteMemoryUsed(id);
  if (sqlite_bytes != -1) {
    out_process->sqlite_memory.reset(new double(static_cast<double>(
        sqlite_bytes)));
  }

  blink::WebCache::ResourceTypeStats cache_stats;
  if (task_manager->GetWebCacheStats(id, &cache_stats)) {
    out_process->image_cache = CreateCacheData(cache_stats.images);
    out_process->script_cache = CreateCacheData(cache_stats.scripts);
    out_process->css_cache = CreateCacheData(cache_stats.cssStyleSheets);
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ProcessesEventRouter:
////////////////////////////////////////////////////////////////////////////////

ProcessesEventRouter::ProcessesEventRouter(content::BrowserContext* context)
    : task_manager::TaskManagerObserver(base::TimeDelta::FromSeconds(1),
                                        task_manager::REFRESH_TYPE_NONE),
      browser_context_(context),
      listeners_(0) {
}

ProcessesEventRouter::~ProcessesEventRouter() {
}

void ProcessesEventRouter::ListenerAdded() {
  UpdateRefreshTypesFlagsBasedOnListeners();

  if (listeners_++ == 0) {
    // The first listener to be added.
    task_manager::TaskManagerInterface::GetTaskManager()->AddObserver(this);
  }
}

void ProcessesEventRouter::ListenerRemoved() {
  UpdateRefreshTypesFlagsBasedOnListeners();

  if (--listeners_ == 0) {
    // Last listener to be removed.
    task_manager::TaskManagerInterface::GetTaskManager()->RemoveObserver(
        this);
  }
}

void ProcessesEventRouter::OnTaskAdded(task_manager::TaskId id) {
  if (!HasEventListeners(api::processes::OnCreated::kEventName))
    return;

  int child_process_host_id = 0;
  if (!ShouldReportOnCreatedOrOnExited(id, &child_process_host_id))
    return;

  api::processes::Process process;
  FillProcessData(id,
                  observed_task_manager(),
                  false,  // include_optional
                  &process);
  DispatchEvent(events::PROCESSES_ON_CREATED,
                api::processes::OnCreated::kEventName,
                api::processes::OnCreated::Create(process));
}

void ProcessesEventRouter::OnTaskToBeRemoved(task_manager::TaskId id) {
  if (!HasEventListeners(api::processes::OnExited::kEventName))
    return;

  int child_process_host_id = 0;
  if (!ShouldReportOnCreatedOrOnExited(id, &child_process_host_id))
    return;

  int exit_code = 0;
  base::TerminationStatus status = base::TERMINATION_STATUS_STILL_RUNNING;
  observed_task_manager()->GetTerminationStatus(id, &status, &exit_code);

  DispatchEvent(events::PROCESSES_ON_EXITED,
                api::processes::OnExited::kEventName,
                api::processes::OnExited::Create(child_process_host_id,
                                                 status,
                                                 exit_code));
}

void ProcessesEventRouter::OnTasksRefreshedWithBackgroundCalculations(
    const task_manager::TaskIdList& task_ids) {
  const bool has_on_updated_listeners =
      HasEventListeners(api::processes::OnUpdated::kEventName);
  const bool has_on_updated_with_memory_listeners =
      HasEventListeners(api::processes::OnUpdatedWithMemory::kEventName);

  if (!has_on_updated_listeners && !has_on_updated_with_memory_listeners)
    return;

  // Get the data of tasks sharing the same process only once.
  std::set<base::ProcessId> seen_processes;
  base::DictionaryValue processes_dictionary;
  for (const auto& task_id : task_ids) {
    // We are not interested in tasks, but rather the processes on which they
    // run.
    const base::ProcessId proc_id =
        observed_task_manager()->GetProcessId(task_id);
    if (seen_processes.count(proc_id))
      continue;

    const int child_process_host_id =
        observed_task_manager()->GetChildProcessUniqueId(task_id);
    // Ignore tasks that don't have a valid child process host ID like ARC
    // processes. We report the browser process info here though.
    if (child_process_host_id == content::ChildProcessHost::kInvalidUniqueID)
      continue;

    seen_processes.insert(proc_id);
    api::processes::Process process;
    FillProcessData(task_id,
                    observed_task_manager(),
                    true,  // include_optional
                    &process);

    if (has_on_updated_with_memory_listeners) {
      // Append the private memory usage to the process data.
      const int64_t private_memory =
          observed_task_manager()->GetPrivateMemoryUsage(task_id);
      process.private_memory.reset(new double(static_cast<double>(
          private_memory)));
    }

    // Store each process indexed by the string version of its ChildProcessHost
    // ID.
    processes_dictionary.Set(base::IntToString(child_process_host_id),
                             process.ToValue());
  }

  // Done with data collection. Now dispatch the appropriate events according to
  // the present listeners.
  DCHECK(has_on_updated_listeners || has_on_updated_with_memory_listeners);
  if (has_on_updated_listeners) {
    api::processes::OnUpdated::Processes processes;
    processes.additional_properties.MergeDictionary(&processes_dictionary);
    // NOTE: If there are listeners to the updates with memory as well,
    // listeners to onUpdated (without memory) will also get the memory info
    // of processes as an added bonus.
    DispatchEvent(events::PROCESSES_ON_UPDATED,
                  api::processes::OnUpdated::kEventName,
                  api::processes::OnUpdated::Create(processes));
  }

  if (has_on_updated_with_memory_listeners) {
    api::processes::OnUpdatedWithMemory::Processes processes;
    processes.additional_properties.MergeDictionary(&processes_dictionary);
    DispatchEvent(events::PROCESSES_ON_UPDATED_WITH_MEMORY,
                  api::processes::OnUpdatedWithMemory::kEventName,
                  api::processes::OnUpdatedWithMemory::Create(processes));
  }
}

void ProcessesEventRouter::OnTaskUnresponsive(task_manager::TaskId id) {
  if (!HasEventListeners(api::processes::OnUnresponsive::kEventName))
    return;

  api::processes::Process process;
  FillProcessData(id,
                  observed_task_manager(),
                  false,  // include_optional
                  &process);
  DispatchEvent(events::PROCESSES_ON_UNRESPONSIVE,
                api::processes::OnUnresponsive::kEventName,
                api::processes::OnUnresponsive::Create(process));
}

void ProcessesEventRouter::DispatchEvent(
    events::HistogramValue histogram_value,
    const std::string& event_name,
    std::unique_ptr<base::ListValue> event_args) const {
  EventRouter* event_router = EventRouter::Get(browser_context_);
  if (event_router) {
    std::unique_ptr<Event> event(
        new Event(histogram_value, event_name, std::move(event_args)));
    event_router->BroadcastEvent(std::move(event));
  }
}

bool ProcessesEventRouter::HasEventListeners(
    const std::string& event_name) const {
  EventRouter* event_router = EventRouter::Get(browser_context_);
  return event_router && event_router->HasEventListener(event_name);
}

bool ProcessesEventRouter::ShouldReportOnCreatedOrOnExited(
    task_manager::TaskId id,
    int* out_child_process_host_id) const {
  // Is it the first task to be created or the last one to be removed?
  if (observed_task_manager()->GetNumberOfTasksOnSameProcess(id) != 1)
    return false;

  // Ignore tasks that don't have a valid child process host ID like ARC
  // processes, as well as the browser process (neither onCreated() nor
  // onExited() shouldn't report the browser process).
  *out_child_process_host_id =
      observed_task_manager()->GetChildProcessUniqueId(id);
  if (*out_child_process_host_id ==
          content::ChildProcessHost::kInvalidUniqueID ||
      *out_child_process_host_id == 0) {
    return false;
  }

  return true;
}

void ProcessesEventRouter::UpdateRefreshTypesFlagsBasedOnListeners() {
  int64_t refresh_types = task_manager::REFRESH_TYPE_NONE;
  if (HasEventListeners(api::processes::OnCreated::kEventName) ||
      HasEventListeners(api::processes::OnUnresponsive::kEventName)) {
    refresh_types |= GetRefreshTypesFlagOnlyEssentialData();
  }

  if (HasEventListeners(api::processes::OnUpdated::kEventName))
    refresh_types |= GetRefreshTypesForProcessOptionalData();

  if (HasEventListeners(api::processes::OnUpdatedWithMemory::kEventName))
    refresh_types |= task_manager::REFRESH_TYPE_MEMORY;

  SetRefreshTypesFlags(refresh_types);
}

////////////////////////////////////////////////////////////////////////////////
// ProcessesAPI:
////////////////////////////////////////////////////////////////////////////////

ProcessesAPI::ProcessesAPI(content::BrowserContext* context)
    : browser_context_(context) {
  EventRouter* event_router = EventRouter::Get(browser_context_);
  // Monitor when the following events are being listened to in order to know
  // when to start the task manager.
  event_router->RegisterObserver(this, api::processes::OnUpdated::kEventName);
  event_router->RegisterObserver(
      this, api::processes::OnUpdatedWithMemory::kEventName);
  event_router->RegisterObserver(this, api::processes::OnCreated::kEventName);
  event_router->RegisterObserver(this,
                                 api::processes::OnUnresponsive::kEventName);
  event_router->RegisterObserver(this, api::processes::OnExited::kEventName);
}

ProcessesAPI::~ProcessesAPI() {
  // This object has already been unregistered as an observer in Shutdown().
}

// static
BrowserContextKeyedAPIFactory<ProcessesAPI>*
ProcessesAPI::GetFactoryInstance() {
  return g_processes_api_factory.Pointer();
}

// static
ProcessesAPI* ProcessesAPI::Get(content::BrowserContext* context) {
  return BrowserContextKeyedAPIFactory<ProcessesAPI>::Get(context);
}

void ProcessesAPI::Shutdown() {
  EventRouter::Get(browser_context_)->UnregisterObserver(this);
}

void ProcessesAPI::OnListenerAdded(const EventListenerInfo& details) {
  // The ProcessesEventRouter will observe the TaskManager as long as there are
  // listeners for the processes.onUpdated/.onUpdatedWithMemory/.onCreated ...
  // etc. events.
  processes_event_router()->ListenerAdded();
}

void ProcessesAPI::OnListenerRemoved(const EventListenerInfo& details) {
  // If a processes.onUpdated/.onUpdatedWithMemory/.onCreated ... etc. event
  // listener is removed (or a process with one exits), then we let the
  // extension API know that it has one fewer listener.
  processes_event_router()->ListenerRemoved();
}

ProcessesEventRouter* ProcessesAPI::processes_event_router() {
  if (!processes_event_router_.get())
    processes_event_router_.reset(new ProcessesEventRouter(browser_context_));
  return processes_event_router_.get();
}

////////////////////////////////////////////////////////////////////////////////
// ProcessesGetProcessIdForTabFunction:
////////////////////////////////////////////////////////////////////////////////

ExtensionFunction::ResponseAction ProcessesGetProcessIdForTabFunction::Run() {
  // For this function, the task manager doesn't even need to be running.
  std::unique_ptr<api::processes::GetProcessIdForTab::Params> params(
      api::processes::GetProcessIdForTab::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  const int tab_id = params->tab_id;
  content::WebContents* contents = nullptr;
  int tab_index = -1;
  if (!ExtensionTabUtil::GetTabById(
          tab_id,
          Profile::FromBrowserContext(browser_context()),
          include_incognito(),
          nullptr,
          nullptr,
          &contents,
          &tab_index)) {
    return RespondNow(Error(tabs_constants::kTabNotFoundError,
                            base::IntToString(tab_id)));
  }

  const int process_id = contents->GetRenderProcessHost()->GetID();
  return RespondNow(ArgumentList(
      api::processes::GetProcessIdForTab::Results::Create(process_id)));
}

////////////////////////////////////////////////////////////////////////////////
// ProcessesTerminateFunction:
////////////////////////////////////////////////////////////////////////////////

ExtensionFunction::ResponseAction ProcessesTerminateFunction::Run() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // For this function, the task manager doesn't even need to be running.
  std::unique_ptr<api::processes::Terminate::Params> params(
      api::processes::Terminate::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  child_process_host_id_ = params->process_id;
  if (child_process_host_id_ < 0) {
    return RespondNow(Error(errors::kInvalidArgument,
                            base::IntToString(child_process_host_id_)));
  } else if (child_process_host_id_ == 0) {
    // Cannot kill the browser process.
    return RespondNow(Error(errors::kNotAllowedToTerminate,
                            base::IntToString(child_process_host_id_)));
  }

  // Check if it's a renderer.
  auto* render_process_host =
      content::RenderProcessHost::FromID(child_process_host_id_);
  if (render_process_host)
    return RespondNow(TerminateIfAllowed(render_process_host->GetHandle()));

  // This could be a non-renderer child process like a plugin or a nacl
  // process. Try to get its handle from the BrowserChildProcessHost on the
  // IO thread.
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::IO,
      FROM_HERE,
      base::Bind(&ProcessesTerminateFunction::GetProcessHandleOnIO,
                 this,
                 child_process_host_id_),
      base::Bind(&ProcessesTerminateFunction::OnProcessHandleOnUI, this));

  // Promise to respond later.
  return RespondLater();
}

base::ProcessHandle ProcessesTerminateFunction::GetProcessHandleOnIO(
    int child_process_host_id) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  auto* host = content::BrowserChildProcessHost::FromID(child_process_host_id);
  if (host)
    return host->GetData().handle;

  return base::kNullProcessHandle;
}

void ProcessesTerminateFunction::OnProcessHandleOnUI(
    base::ProcessHandle handle) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  Respond(TerminateIfAllowed(handle));
}

ExtensionFunction::ResponseValue
ProcessesTerminateFunction::TerminateIfAllowed(base::ProcessHandle handle) {
  if (handle == base::kNullProcessHandle) {
    return Error(errors::kProcessNotFound,
                 base::IntToString(child_process_host_id_));
  }

  if (handle == base::GetCurrentProcessHandle()) {
    // Cannot kill the browser process.
    return Error(errors::kNotAllowedToTerminate,
                 base::IntToString(child_process_host_id_));
  }

  base::Process process = base::Process::Open(base::GetProcId(handle));
  if (!process.IsValid()) {
    return Error(errors::kProcessNotFound,
                 base::IntToString(child_process_host_id_));
  }

  const bool did_terminate =
      process.Terminate(content::RESULT_CODE_KILLED, true /* wait */);
  if (did_terminate)
    UMA_HISTOGRAM_COUNTS("ChildProcess.KilledByExtensionAPI", 1);

  return ArgumentList(
      api::processes::Terminate::Results::Create(did_terminate));
}

////////////////////////////////////////////////////////////////////////////////
// ProcessesGetProcessInfoFunction:
////////////////////////////////////////////////////////////////////////////////

ProcessesGetProcessInfoFunction::ProcessesGetProcessInfoFunction()
    : task_manager::TaskManagerObserver(
          base::TimeDelta::FromSeconds(1),
          GetRefreshTypesFlagOnlyEssentialData()) {
}

ExtensionFunction::ResponseAction ProcessesGetProcessInfoFunction::Run() {
  std::unique_ptr<api::processes::GetProcessInfo::Params> params(
      api::processes::GetProcessInfo::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  if (params->process_ids.as_integer)
    process_host_ids_.push_back(*params->process_ids.as_integer);
  else
    process_host_ids_.swap(*params->process_ids.as_integers);

  include_memory_ = params->include_memory;
  if (include_memory_)
    AddRefreshType(task_manager::REFRESH_TYPE_MEMORY);

  // Keep this object alive until the first of either OnTasksRefreshed() or
  // OnTasksRefreshedWithBackgroundCalculations() is received depending on
  // |include_memory_|.
  AddRef();

  // The task manager needs to be enabled for this function.
  // Start observing the task manager and wait for the next refresh event.
  task_manager::TaskManagerInterface::GetTaskManager()->AddObserver(this);

  return RespondLater();
}

void ProcessesGetProcessInfoFunction::OnTasksRefreshed(
    const task_manager::TaskIdList& task_ids) {
  // Memory is background calculated and will be ready when
  // OnTasksRefreshedWithBackgroundCalculations() is invoked.
  if (include_memory_)
    return;

  GatherDataAndRespond(task_ids);
}

void
ProcessesGetProcessInfoFunction::OnTasksRefreshedWithBackgroundCalculations(
    const task_manager::TaskIdList& task_ids) {
  if (!include_memory_)
    return;

  GatherDataAndRespond(task_ids);
}

ProcessesGetProcessInfoFunction::~ProcessesGetProcessInfoFunction() {}

void ProcessesGetProcessInfoFunction::GatherDataAndRespond(
    const task_manager::TaskIdList& task_ids) {
  // If there are no process IDs specified, it means we need to return all of
  // the ones we know of.
  const bool specific_processes_requested = !process_host_ids_.empty();
  std::set<base::ProcessId> seen_processes;
  // Create the results object as defined in the generated API from process.idl
  // and fill it with the processes info.
  api::processes::GetProcessInfo::Results::Processes processes;
  for (const auto& task_id : task_ids) {
    const base::ProcessId proc_id =
        observed_task_manager()->GetProcessId(task_id);
    if (seen_processes.count(proc_id))
      continue;

    const int child_process_host_id =
        observed_task_manager()->GetChildProcessUniqueId(task_id);
    // Ignore tasks that don't have a valid child process host ID like ARC
    // processes. We report the browser process info here though.
    if (child_process_host_id == content::ChildProcessHost::kInvalidUniqueID)
      continue;

    if (specific_processes_requested) {
      // Note: we can't use |!process_host_ids_.empty()| directly in the above
      // condition as we will erase from |process_host_ids_| below.
      auto itr = std::find(process_host_ids_.begin(),
                           process_host_ids_.end(),
                           child_process_host_id);
      if (itr == process_host_ids_.end())
        continue;

      // If found, we remove it from |process_host_ids|, so that at the end if
      // anything remains in |process_host_ids|, those were invalid arguments
      // that will be reported on the console.
      process_host_ids_.erase(itr);
    }

    seen_processes.insert(proc_id);

    // We do not include the optional data in this function results.
    api::processes::Process process;
    FillProcessData(task_id,
                    observed_task_manager(),
                    false,  // include_optional
                    &process);

    if (include_memory_) {
      // Append the private memory usage to the process data.
      const int64_t private_memory =
          observed_task_manager()->GetPrivateMemoryUsage(task_id);
      process.private_memory.reset(new double(static_cast<double>(
          private_memory)));
    }

    // Store each process indexed by the string version of its
    // ChildProcessHost ID.
    processes.additional_properties.Set(
        base::IntToString(child_process_host_id),
        process.ToValue());
  }

  // Report the invalid host ids sent in the arguments.
  for (const auto& host_id : process_host_ids_) {
    WriteToConsole(content::CONSOLE_MESSAGE_LEVEL_ERROR,
                   ErrorUtils::FormatErrorMessage(errors::kProcessNotFound,
                                                  base::IntToString(host_id)));
  }

  // Send the response.
  Respond(ArgumentList(
      api::processes::GetProcessInfo::Results::Create(processes)));

  // Stop observing the task manager, and balance the AddRef() in Run().
  task_manager::TaskManagerInterface::GetTaskManager()->RemoveObserver(this);
  Release();
}

}  // namespace extensions
