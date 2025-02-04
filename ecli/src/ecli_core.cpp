/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 *
 * Copyright (c) 2022, 郑昱笙，濮雯旭，张典典（牛校牛子队）
 * All rights reserved.
 */

#include <signal.h>
#include <unistd.h>

#include <json.hpp>
#include <optional>

#include "ecli/ecli_core.h"
#include "ecli/eunomia_runner.h"
#include "ecli/tracker_manager.h"
#include "ecli/url_resolver.h"
#include "spdlog/spdlog.h"

using json = nlohmann::json;

ecli_core::ecli_core(eunomia_config_data& config) : core_config(config)
{
}

eunomia_runner::tracker_event_handler ecli_core::create_tracker_event_handler(const handler_config_data& config)
{
  // spdlog::info("create event handler for {}", config.name);
  if (config.name == "plain_text")
  {
    return std::make_shared<typename eunomia_runner::plain_text_event_printer>();
  }
  else if (config.name == "none")
  {
    return nullptr;
  }
  else
  {
    spdlog::error("unsupported event handler {}", config.name);
    return nullptr;
  }
}

eunomia_runner::tracker_event_handler ecli_core::create_tracker_event_handlers(
    const std::vector<handler_config_data>& handler_configs)
{
  eunomia_runner::tracker_event_handler handler = nullptr, base_handler = nullptr;
  for (auto& config : handler_configs)
  {
    auto new_handler = create_tracker_event_handler(config);
    if (new_handler)
    {
      if (handler)
      {
        handler->add_handler(new_handler);
        handler = new_handler;
      }
      else
      {
        handler = new_handler;
        base_handler = new_handler;
      }
    }
  }
  return base_handler;
}

// create a default tracker with other handlers
std::unique_ptr<eunomia_runner> ecli_core::create_tracker_with_handler(
    const tracker_config_data& base,
    eunomia_runner::tracker_event_handler additional_handler)
{
  auto handler = create_tracker_event_handlers(base.export_handlers);
  if (!handler && !additional_handler)
  {
    spdlog::info("no handler was created for tracker");
  }
  if (additional_handler)
  {
    additional_handler->add_handler(handler);
    handler = additional_handler;
  }
  auto json_data = resolve_json_data(base);
  if (!json_data)
  {
    return nullptr;
  }
  return eunomia_runner::create_tracker_with_args(handler, base.url, *json_data, base.args);
}

std::unique_ptr<eunomia_runner> ecli_core::create_default_tracker(const tracker_config_data& base)
{
  return create_tracker_with_handler(base, nullptr);
}

std::vector<std::tuple<int, std::string>> ecli_core::list_all_trackers(void)
{
  return core_tracker_manager.get_tracker_list();
}

void ecli_core::stop_tracker(std::size_t tracker_id)
{
  core_tracker_manager.remove_tracker(tracker_id);
}

std::size_t ecli_core::start_tracker(const tracker_config_data& config)
{
  spdlog::info("tracker is starting from {}...", config.url);
  auto tracker = create_default_tracker(config);
  if (!tracker)
  {
    return 0;
  }
  return core_tracker_manager.start_tracker(std::move(tracker), config.url);
}

std::size_t ecli_core::start_tracker(const std::string& json_data)
{
  spdlog::info("tracker is starting...");
  auto tracker = create_default_tracker(tracker_config_data{ "", json_data, {}, {} });
  if (!tracker)
  {
    return 0;
  }
  spdlog::info("tracker name: {}", tracker->get_name());
  return core_tracker_manager.start_tracker(std::move(tracker), tracker->get_name());
}

std::size_t ecli_core::start_trackers(void)
{
  std::size_t tracker_count = 0;
  for (auto& t : core_config.enabled_trackers)
  {
    spdlog::info("start ebpf tracker...");
    tracker_count += static_cast<std::size_t>(start_tracker(t) > 0 ? 1 : 0);
  }
  return tracker_count;
}

void ecli_core::check_auto_exit(std::size_t checker_count)
{
  if (core_config.exit_after > 0)
  {
    spdlog::info("set exit time...");
    std::this_thread::sleep_for(std::chrono::seconds(core_config.exit_after));
    // do nothing in server mode
  }
  else
  {
    if (core_config.run_selected != "server" && checker_count > 0)
    {
      spdlog::info("press 'Ctrl C' key to exit...");
      static bool is_exiting = false;
      signal(
          SIGINT,
          [](int x)
          {
            spdlog::info("Ctrl C exit...");
            is_exiting = true;
            signal(SIGINT, SIG_DFL);
          });
      while (!is_exiting)
      {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  }
}

int ecli_core::start_eunomia(void)
{
  spdlog::info("start eunomia...");
  try
  {
    std::size_t checker_count = start_trackers();
    check_auto_exit(checker_count);
  }
  catch (const std::exception& e)
  {
    spdlog::error("eunomia start failed: {}", e.what());
    return 1;
  }
  spdlog::debug("eunomia exit. see you next time!");
  return 0;
}
