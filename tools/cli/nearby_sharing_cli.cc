// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include "absl/time/time.h"
#include "connections/flags/nearby_connections_feature_flags.h"
#include "base/base/file_path.h"
#include "base/flags/nearby_flags.h"
#include "sharing/domain/advertisement.h"
#include "sharing/domain/attachment_container.h"
#include "sharing/api/common/nearby_share_enums.h"
#include "sharing/domain/file_attachment.h"
#include "sharing/linux/platform/linux_sharing_platform.h"
#include "sharing/linux/nearby_noop_analytics_recorder.h"
#include "sharing/service/flags/generated/nearby_sharing_feature_flags.h"
#include "sharing/service/nearby_sharing_service.h"
#include "sharing/service/nearby_sharing_service_factory.h"
#include "sharing/service/nearby_sharing_settings.h"
#include "sharing/domain/share_target.h"
#include "sharing/service/share_target_discovered_callback.h"
#include "sharing/domain/transfer_metadata.h"
#include "sharing/service/transfer_update_callback.h"
#include "proto/sharing/enums.pb.h"

namespace quickshare::app {
namespace {

std::atomic<bool> g_interrupted = false;

void HandleSignal(int signal) {
  static_cast<void>(signal);
  g_interrupted = true;
}

std::string GetHostname() {
  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname)) == 0 && hostname[0] != '\0') {
    return std::string(hostname);
  }
  return "Linux";
}

void PrintUsage(const char* argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0 << " receive [--name NAME] [--timeout SECONDS]\n"
            << "  " << argv0
            << " send FILE [--name NAME] [--timeout SECONDS]\n";
}

struct Options {
  enum class Mode { kReceive, kSend };

  Mode mode;
  std::string file_path;
  std::string device_name = GetHostname();
  int timeout_seconds = 120;
};

std::optional<Options> ParseArgs(int argc, char** argv) {
  if (argc < 2) {
    return std::nullopt;
  }

  Options options{.mode = Options::Mode::kReceive};
  std::string command = argv[1];
  int index = 2;
  if (command == "receive") {
    options.mode = Options::Mode::kReceive;
  } else if (command == "send") {
    options.mode = Options::Mode::kSend;
    if (index >= argc) {
      return std::nullopt;
    }
    options.file_path = argv[index++];
  } else {
    return std::nullopt;
  }

  while (index < argc) {
    std::string arg = argv[index++];
    if (arg == "--name") {
      if (index >= argc) {
        return std::nullopt;
      }
      options.device_name = argv[index++];
    } else if (arg == "--timeout") {
      if (index >= argc) {
        return std::nullopt;
      }
      options.timeout_seconds = std::atoi(argv[index++]);
      if (options.timeout_seconds < 0) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }

  return options;
}

std::string StatusCodeToString(sharing::NearbySharingService::StatusCodes status) {
  return sharing::NearbySharingService::StatusCodeToString(status);
}

std::unique_ptr<sharing::AttachmentContainer> CreateFileAttachments(
    const std::string& file_path) {
  sharing::AttachmentContainer::Builder builder;
  builder.AddFileAttachment(sharing::FileAttachment(base::FilePath(file_path)));
  return builder.Build();
}

template <typename Invoker>
sharing::NearbySharingService::StatusCodes WaitForStatus(Invoker invoker) {
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<sharing::NearbySharingService::StatusCodes> status;
  invoker([&](sharing::NearbySharingService::StatusCodes callback_status) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      status = callback_status;
    }
    cv.notify_one();
  });

  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return status.has_value(); });
  return *status;
}

struct CliState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool send_requested = false;
  bool accept_requested = false;
  int exit_code = 1;
  std::optional<sharing::ShareTarget> selected_target;
  std::optional<int64_t> accept_target_id;
};

void PrintTransferUpdate(const sharing::ShareTarget& share_target,
                         const sharing::AttachmentContainer& attachment_container,
                         const sharing::TransferMetadata& metadata) {
  std::cout << "transfer target=\"" << share_target.device_name
            << "\" id=" << share_target.id
            << " status=" << sharing::TransferMetadata::StatusToString(metadata.status())
            << " progress=" << metadata.progress()
            << "% bytes=" << metadata.transferred_bytes() << "/"
            << attachment_container.GetTotalAttachmentsSize() << std::endl;
  if (metadata.token().has_value()) {
    std::cout << "confirmation token: " << *metadata.token() << std::endl;
  }
}

class CliTransferCallback final : public sharing::TransferUpdateCallback {
 public:
  CliTransferCallback(CliState& state, bool receive_mode)
      : state_(state), receive_mode_(receive_mode) {}

  void OnTransferUpdate(const sharing::ShareTarget& share_target,
                        const sharing::AttachmentContainer& attachment_container,
                        const sharing::TransferMetadata& transfer_metadata) override {
    PrintTransferUpdate(share_target, attachment_container, transfer_metadata);
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (receive_mode_ &&
        transfer_metadata.status() ==
            sharing::TransferMetadata::Status::kAwaitingLocalConfirmation &&
        !state_.accept_requested) {
      state_.accept_requested = true;
      state_.accept_target_id = share_target.id;
    }
    if (sharing::TransferMetadata::IsFinalStatus(transfer_metadata.status())) {
      state_.done = true;
      state_.exit_code =
          transfer_metadata.status() == sharing::TransferMetadata::Status::kComplete ? 0
                                                                            : 1;
    }
    state_.cv.notify_all();
  }

 private:
  CliState& state_;
  bool receive_mode_;
};

class CliDiscoveryCallback final : public sharing::ShareTargetDiscoveredCallback {
 public:
  explicit CliDiscoveryCallback(CliState& state) : state_(state) {}

  void OnShareTargetDiscovered(const sharing::ShareTarget& share_target) override {
    std::cout << "discovered target=\"" << share_target.device_name
              << "\" id=" << share_target.id << std::endl;
    if (share_target.receive_disabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(state_.mutex);
    if (!state_.selected_target.has_value() && !state_.send_requested) {
      state_.selected_target = share_target;
    }
    state_.cv.notify_all();
  }

  void OnShareTargetLost(const sharing::ShareTarget& share_target) override {
    std::cout << "lost target=\"" << share_target.device_name
              << "\" id=" << share_target.id << std::endl;
  }

  void OnShareTargetUpdated(const sharing::ShareTarget& share_target) override {
    std::cout << "updated target=\"" << share_target.device_name
              << "\" id=" << share_target.id << std::endl;
    OnShareTargetDiscovered(share_target);
  }

 private:
  CliState& state_;
};

class CliApp {
 public:
  explicit CliApp(const Options& options)
      : options_(options), platform_(options.device_name) {}

  int Run() {
    service_ = sharing::NearbySharingServiceFactory::GetInstance()->CreateSharingService(
        platform_, &analytics_recorder_, /*event_logger=*/nullptr,
        /*supports_file_sync=*/false);
    if (service_ == nullptr) {
      std::cerr << "failed to create NearbySharingService" << std::endl;
      return 1;
    }

    if (!ConfigureService()) {
      Shutdown();
      return 1;
    }
    int result =
        options_.mode == Options::Mode::kReceive ? RunReceive() : RunSend();
    Shutdown();
    return result;
  }

 private:
  bool ConfigureService() {
    std::cout << "device name: " << options_.device_name << std::endl;
    service_->GetSettings()->SetDataUsage(nearby::sharing::proto::WIFI_ONLY_DATA_USAGE);
    service_->GetSettings()->SetDeviceName(
        options_.device_name, [](sharing::DeviceNameValidationResult validation_result) {
          static_cast<void>(validation_result);
        });
    auto status = WaitForStatus([&](auto callback) {
      service_->SetVisibility(nearby::sharing::proto::DEVICE_VISIBILITY_EVERYONE,
                              absl::ZeroDuration(), std::move(callback));
    });
    std::cout << "SetVisibility: " << StatusCodeToString(status) << std::endl;
    return status == sharing::NearbySharingService::StatusCodes::kOk;
  }

  int RunReceive() {
    CliState state;
    CliTransferCallback transfer_callback(state, /*receive_mode=*/true);

    auto status = WaitForStatus([&](auto callback) {
      service_->RegisterReceiveSurface(
          &transfer_callback,
          sharing::NearbySharingService::ReceiveSurfaceState::kForeground,
          sharing::Advertisement::BlockedVendorId::kNone, std::move(callback));
    });
    std::cout << "RegisterReceiveSurface: " << StatusCodeToString(status)
              << std::endl;
    if (status != sharing::NearbySharingService::StatusCodes::kOk) {
      return 1;
    }

    std::cout << "receiving; waiting for incoming share" << std::endl;
    int result = WaitForTransferOrActions(state);
    WaitForStatus([&](auto callback) {
      service_->UnregisterReceiveSurface(&transfer_callback,
                                         std::move(callback));
    });
    return result;
  }

  int RunSend() {
    std::error_code error;
    if (!std::filesystem::is_regular_file(options_.file_path, error)) {
      std::cerr << "not a regular file: " << options_.file_path << std::endl;
      return 1;
    }

    CliState state;
    CliTransferCallback transfer_callback(state, /*receive_mode=*/false);
    CliDiscoveryCallback discovery_callback(state);

    auto status = WaitForStatus([&](auto callback) {
      service_->RegisterSendSurface(
          &transfer_callback, &discovery_callback,
          sharing::NearbySharingService::SendSurfaceState::kForeground,
          sharing::Advertisement::BlockedVendorId::kNone,
          /*disable_wifi_hotspot=*/false, std::move(callback));
    });
    std::cout << "RegisterSendSurface: " << StatusCodeToString(status)
              << std::endl;
    if (status != sharing::NearbySharingService::StatusCodes::kOk) {
      return 1;
    }

    std::cout << "scanning; waiting for first target" << std::endl;
    int result = WaitForTransferOrActions(state);
    WaitForStatus([&](auto callback) {
      service_->UnregisterSendSurface(&transfer_callback, std::move(callback));
    });
    return result;
  }

  int WaitForTransferOrActions(CliState& state) {
    const auto start = std::chrono::steady_clock::now();
    while (!g_interrupted) {
      std::optional<int64_t> accept_target_id;
      std::optional<sharing::ShareTarget> send_target;
      {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (options_.timeout_seconds == 0) {
          state.cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return state.done || state.accept_target_id.has_value() ||
                   state.selected_target.has_value() || g_interrupted.load();
          });
        } else {
          auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - start);
          if (elapsed.count() >= options_.timeout_seconds) {
            std::cerr << "timed out after " << options_.timeout_seconds
                      << " seconds" << std::endl;
            return 1;
          }
          state.cv.wait_for(lock, std::chrono::seconds(1), [&] {
            return state.done || state.accept_target_id.has_value() ||
                   state.selected_target.has_value() || g_interrupted.load();
          });
        }
        if (state.done) {
          return state.exit_code;
        }
        if (state.accept_target_id.has_value()) {
          accept_target_id = state.accept_target_id;
          state.accept_target_id.reset();
        }
        if (state.selected_target.has_value() && !state.send_requested) {
          send_target = state.selected_target;
          state.send_requested = true;
        }
      }

      if (accept_target_id.has_value()) {
        std::cout << "accepting incoming share id=" << *accept_target_id
                  << std::endl;
        auto status = WaitForStatus([&](auto callback) {
          service_->Accept(*accept_target_id, std::move(callback));
        });
        std::cout << "Accept: " << StatusCodeToString(status) << std::endl;
        if (status != sharing::NearbySharingService::StatusCodes::kOk) {
          return 1;
        }
      }

      if (send_target.has_value()) {
        std::cout << "sending " << options_.file_path << " to \""
                  << send_target->device_name << "\" id=" << send_target->id
                  << std::endl;
        auto attachments = CreateFileAttachments(options_.file_path);
        auto status = WaitForStatus([&](auto callback) {
          service_->SendAttachments(send_target->id, std::move(attachments),
                                    std::move(callback));
        });
        std::cout << "SendAttachments: " << StatusCodeToString(status)
                  << std::endl;
        if (status != sharing::NearbySharingService::StatusCodes::kOk) {
          return 1;
        }
      }
    }

    std::cerr << "interrupted" << std::endl;
    return 1;
  }

  void Shutdown() {
    if (service_ == nullptr) {
      return;
    }
    auto status = WaitForStatus(
        [&](auto callback) { service_->Shutdown(std::move(callback)); });
    std::cout << "Shutdown: " << StatusCodeToString(status) << std::endl;
  }

  Options options_;
  sharing::linux::LinuxSharingPlatform platform_;
  sharing::linux::NoOpAnalyticsRecorder analytics_recorder_;
  sharing::NearbySharingService* service_ = nullptr;
};

}  // namespace
}  // namespace quickshare::app

int main(int argc, char** argv) {
  signal(SIGINT, quickshare::app::HandleSignal);
  signal(SIGTERM, quickshare::app::HandleSignal);
  quickshare::base::NearbyFlags::GetInstance().OverrideBoolFlagValue(
      quickshare::sharing::config_package_nearby::nearby_sharing_feature::kEnableBleForTransfer,
      true);
  quickshare::base::NearbyFlags::GetInstance().OverrideBoolFlagValue(
      quickshare::connections::config_package_nearby::nearby_connections_feature::kEnableBleL2cap,
      true);
  quickshare::base::NearbyFlags::GetInstance().OverrideBoolFlagValue(
      quickshare::connections::config_package_nearby::nearby_connections_feature::kRefactorBleL2cap,
      true);
  quickshare::base::NearbyFlags::GetInstance().OverrideBoolFlagValue(
      quickshare::connections::config_package_nearby::nearby_connections_feature::kEnableWifiDirect,
      true);
  quickshare::base::NearbyFlags::GetInstance().OverrideBoolFlagValue(
      quickshare::connections::config_package_nearby::nearby_connections_feature::kEnableWifiDirectGcOnly,
      true);

  std::optional<quickshare::app::Options> options =
      quickshare::app::ParseArgs(argc, argv);
  if (!options.has_value()) {
    quickshare::app::PrintUsage(argv[0]);
    return 2;
  }

  if (options->mode == quickshare::app::Options::Mode::kSend) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(options->file_path, error)) {
      std::cerr << "not a regular file: " << options->file_path << std::endl;
      return 1;
    }
  }

  quickshare::app::CliApp app(*options);
  return app.Run();
}
