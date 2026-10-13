// Copyright 2021 Google LLC
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

#include "sharing/service/scheduling/nearby_share_scheduler.h"

#include <utility>

namespace quickshare::sharing {

NearbyShareScheduler::NearbyShareScheduler(OnRequestCallback callback)
    : callback_(std::move(callback)) {}

void NearbyShareScheduler::Start() {
  is_running_ = true;
  OnStart();
}

void NearbyShareScheduler::Stop() {
  is_running_ = false;
  OnStop();
}

void NearbyShareScheduler::NotifyOfRequest() {
  if (callback_ != nullptr) {
    callback_();
  }
}

}  // namespace quickshare::sharing
