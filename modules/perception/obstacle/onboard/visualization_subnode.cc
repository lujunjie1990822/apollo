/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/perception/obstacle/onboard/visualization_subnode.h"

#include <vector>
#include <string>
#include <map>
#include "modules/common/log.h"
#include "modules/perception/lib/config_manager/calibration_config_manager.h"
#include "modules/perception/obstacle/camera/common/util.h"
#include "modules/perception/obstacle/camera/visualizer/base_visualizer.h"
#include "modules/perception/obstacle/camera/visualizer/frame_content.h"
#include "modules/perception/onboard/event_manager.h"
#include "modules/perception/onboard/shared_data_manager.h"
#include "modules/perception/onboard/subnode_helper.h"

namespace apollo {
namespace perception {

using apollo::common::ErrorCode;
using apollo::common::Status;

bool VisualizationSubnode::InitInternal() {
  CHECK(shared_data_manager_ != NULL);
  // init radar object data
  if (FLAGS_show_radar_objects) {
    radar_object_data_ = dynamic_cast<RadarObjectData*>(
        shared_data_manager_->GetSharedData("RadarObjectData"));
    if (radar_object_data_ == nullptr) {
      AERROR << "Failed to get RadarObjectData.";
      return false;
    }
    AINFO << "Init shared datas successfully, data: "
          << radar_object_data_->name();
  }

  // init camera object data
  if (FLAGS_show_camera_objects || FLAGS_show_camera_objects2d ||
      FLAGS_show_camera_parsing) {
    camera_object_data_ = dynamic_cast<CameraObjectData*>(
        shared_data_manager_->GetSharedData("CameraObjectData"));
    if (camera_object_data_ == nullptr) {
      AERROR << "Failed to get CameraObjectData.";
      return false;
    }
    cipv_object_data_ = dynamic_cast<CIPVObjectData*>(
        shared_data_manager_->GetSharedData("CIPVObjectData"));
    if (cipv_object_data_ == nullptr) {
      AERROR << "Failed to get CIPVObjectData.";
      return false;
    }
    AINFO << "Init shared datas successfully, data: "
          << camera_object_data_->name();
  }

  // init fusion data
  if (FLAGS_show_fused_objects) {
    fusion_data_ = dynamic_cast<FusionSharedData*>(
        shared_data_manager_->GetSharedData("FusionSharedData"));
    if (fusion_data_ == nullptr) {
      AERROR << "Failed to get FusionSharedDataData.";
      return false;
    }
    AINFO << "Init shared datas successfully, data: " << fusion_data_->name();
  }

  // init camera shared data
  if (FLAGS_show_camera_objects || FLAGS_show_camera_objects2d ||
      FLAGS_show_camera_parsing) {
    camera_shared_data_ = dynamic_cast<CameraSharedData*>(
        shared_data_manager_->GetSharedData("CameraSharedData"));
    if (camera_shared_data_ == nullptr) {
      AERROR << "Failed to get CameraSharedData.";
      return false;
    }
    AINFO << "Init shared datas successfully, data: "
          << camera_shared_data_->name();
  }
  // init frame_visualizer
  frame_visualizer_.reset(
      BaseVisualizerRegisterer::GetInstanceByName(FLAGS_frame_visualizer));
  if (!frame_visualizer_) {
    AERROR << "Failed to get instance: " << FLAGS_frame_visualizer;
    return false;
  }
  content_.set_pose_type(FrameContent::IMAGE_CONTINUOUS);
  AINFO << "visualize according to continuous image: ";
  // init stream
  if (!InitStream()) {
    AERROR << "Failed to init stream.";
    return false;
  }

  CalibrationConfigManager* config_manager =
      Singleton<CalibrationConfigManager>::get();
  CameraCalibrationPtr calibrator = config_manager->get_camera_calibration();
  camera_to_car_pose_ = calibrator->get_camera_extrinsics();
  AINFO << "Init camera to car transform successfully.";
  content_.set_camera2car_pose(camera_to_car_pose_);
  return true;
}

bool VisualizationSubnode::InitStream() {
  std::map<std::string, std::string> reserve_field_map;
  if (!SubnodeHelper::ParseReserveField(reserve_, &reserve_field_map)) {
    AERROR << "Failed to parse reserve string: " << reserve_;
    return false;
  }

  auto iter = reserve_field_map.find("vis_driven_event_id");
  if (iter == reserve_field_map.end()) {
    AERROR << "Failed to find vis_driven_event_id:" << reserve_;
    return false;
  }
  vis_driven_event_id_ = static_cast<EventID>(atoi((iter->second).c_str()));

  auto radar_iter = reserve_field_map.find("radar_event_id");
  if (radar_iter == reserve_field_map.end()) {
    AERROR << "Failed to find radar_event_id:" << reserve_;
    return false;
  }
  radar_event_id_ = static_cast<EventID>(atoi((radar_iter->second).c_str()));

  auto camera_iter = reserve_field_map.find("camera_event_id");
  if (camera_iter == reserve_field_map.end()) {
    AERROR << "Failed to find camera_event_id:" << reserve_;
    return false;
  }
  camera_event_id_ = static_cast<EventID>(atoi((camera_iter->second).c_str()));

  auto cipv_iter = reserve_field_map.find("cipv_event_id");
  if (cipv_iter == reserve_field_map.end()) {
    AERROR << "Failed to find cipv_event_id:" << reserve_;
    return false;
  }
  cipv_event_id_ = static_cast<EventID>(atoi((cipv_iter->second).c_str()));

  auto fusion_iter = reserve_field_map.find("fusion_event_id");
  if (fusion_iter == reserve_field_map.end()) {
    AERROR << "Failed to find fusion_event_id:" << reserve_;
    return false;
  }
  fusion_event_id_ = static_cast<EventID>(atoi((fusion_iter->second).c_str()));

  auto motion_iter = reserve_field_map.find("motion_event_id");
  if (motion_iter == reserve_field_map.end()) {
    AERROR << "Failed to find motion_event_id:" << reserve_;
    motion_event_id_ = -1;
  } else {
    motion_event_id_ =
        static_cast<EventID>(atoi((motion_iter->second).c_str()));
  }

  return true;
}

bool VisualizationSubnode::SubscribeEvents(const EventMeta& event_meta,
                                           std::vector<Event>* events) const {
  Event event;
  if (event_meta.event_id == vis_driven_event_id_) {
    // blocking
    //        if (!event_manager_->subscribe(event_meta.event_id, &event, true))
    //        {
    //            // AERROR << "Failed to subscribe event: " <<
    //            event_meta.event_id;
    //            // return false;
    //        }
    event_manager_->Subscribe(event_meta.event_id, &event);
    events->push_back(event);
  } else {
    // no blocking
    while (event_manager_->Subscribe(event_meta.event_id, &event, true)) {
      events->push_back(event);
    }
  }

  return true;
}

void VisualizationSubnode::GetFrameData(const Event& event,
                                        const std::string& device_id,
                                        const std::string& data_key,
                                        const double timestamp,
                                        FrameContent* content) {
  if (event.event_id == camera_event_id_) {
    if (FLAGS_show_camera_objects || FLAGS_show_camera_objects2d ||
        FLAGS_show_camera_parsing) {
      std::shared_ptr<CameraItem> camera_item;
      if (!camera_shared_data_->Get(data_key, &camera_item) ||
          camera_item == nullptr) {
        AERROR << "Failed to get shared data: " << camera_shared_data_->name();
        return;
      }
      cv::Mat clone_image = camera_item->image_src_mat;
      cv::Mat image = camera_item->image_src_mat.clone();
      content->set_image_content(timestamp, image);

      std::shared_ptr<SensorObjects> objs;
      if (!camera_object_data_->Get(data_key, &objs) || objs == nullptr) {
        AERROR << "Failed to get shared data: " << camera_object_data_->name();
        return;
      }

      LOG(INFO) << objs->objects.size() << timestamp;

      //   content->set_camera2velo_pose(_camera_to_velo64_pose);

      if (FLAGS_show_camera_parsing) {
        // content->set_camera_content(timestamp, objs->sensor2world_pose,
        //                            objs->objects,
        //                            (*(objs->camera_frame_supplement)));
      } else {
        content->set_camera_content(timestamp, objs->sensor2world_pose,
                                    objs->objects);
      }
    }
  } else if (event.event_id == motion_event_id_) {
    /*std::shared_ptr<CameraItem> camera_item;
    AERROR << "Motion_Visualization key: in Motion Visualization: " << data_key;
    if (!camera_shared_data_->Get(data_key, &camera_item) ||
        camera_item == nullptr) {
      AERROR << "Failed to get shared data in Motion Visualization: "
             << camera_shared_data_->name() << " " << data_key;
      return;
    }*/

    // content->set_motion_content(timestamp, camera_item->motion_buffer);

    //        std::cout<< "motion_buffer.size(): " <<
    //        camera_item->motion_buffer->size() << std::endl;

  } else if (event.event_id == radar_event_id_) {
    if (device_id == "radar_front" && FLAGS_show_radar_objects) {
      std::shared_ptr<SensorObjects> objs;
      if (!radar_object_data_->Get(data_key, &objs) || objs == nullptr) {
        AERROR << "Failed to get shared data: " << radar_object_data_->name();
        return;
      }
      content->set_radar_content(timestamp, objs->objects);
    }
  } else if (event.event_id == fusion_event_id_) {
    if (FLAGS_show_fused_objects) {
      AINFO << "vis_driven_event data_key = " << data_key;
      SharedDataPtr<FusionItem> fusion_item;
      if (!fusion_data_->Get(data_key, &fusion_item) ||
          fusion_item == nullptr) {
        AERROR << "Failed to get shared data: " << fusion_data_->name();
        return;
      }
      content->set_fusion_content(timestamp, fusion_item->obstacles);

      AINFO << "Set fused objects : " << fusion_item->obstacles.size();
    }
  } else if (event.event_id == cipv_event_id_) {
    if (FLAGS_show_camera_objects || FLAGS_show_camera_objects2d ||
        FLAGS_show_camera_parsing) {
      std::shared_ptr<CameraItem> camera_item;
      if (!camera_shared_data_->Get(data_key, &camera_item) ||
          camera_item == nullptr) {
        AERROR << "Failed to get shared data: " << camera_shared_data_->name();
        return;
      }
      cv::Mat clone_image = camera_item->image_src_mat;
      cv::Mat image = camera_item->image_src_mat.clone();
      content->set_image_content(timestamp, image);

      std::shared_ptr<SensorObjects> objs;
      if (!cipv_object_data_->Get(data_key, &objs) || objs == nullptr) {
        AERROR << "Failed to get shared data: " << cipv_object_data_->name();
        return;
      }

      LOG(INFO) << "number of objects in cipv is " << objs->objects.size()
                << timestamp << " with cipv index is " << objs->cipv_index;

      //   content->set_camera2velo_pose(_camera_to_velo64_pose);

      if (FLAGS_show_camera_parsing) {
        // content->set_camera_content(timestamp, objs->sensor2world_pose,
        //                            objs->objects,
        //                            (*(objs->camera_frame_supplement)));
      } else {
        content->set_camera_content(timestamp, objs->sensor2world_pose,
                                    objs->objects);
      }
    }
  }

  if (event.event_id == vis_driven_event_id_) {
    // vis_driven_event_id_ fusion -> visualization
    content->update_timestamp(timestamp);
  }
}

apollo::common::Status VisualizationSubnode::ProcEvents() {
  for (auto event_meta : sub_meta_events_) {
    std::vector<Event> events;
    if (!SubscribeEvents(event_meta, &events)) {
      return Status(ErrorCode::PERCEPTION_ERROR, "Failed to proc events.");
    }
    if (events.empty()) {
      continue;
    }
    for (size_t j = 0; j < events.size(); j++) {
      double timestamp = events[j].timestamp;
      const std::string& device_id = events[j].reserve;
      std::string data_key;

      if (!SubnodeHelper::ProduceSharedDataKey(timestamp, device_id,
                                               &data_key)) {
        AERROR << "Failed to produce shared data key. timestamp:" << timestamp
               << " device_id:" << device_id;
        return Status(ErrorCode::PERCEPTION_ERROR, "Failed to proc events.");
      }
      //            GetFrameData(device_id, data_key, &content_, timestamp);
      if (event_meta.event_id == vis_driven_event_id_) {
        AERROR << "vis_driven_event_1: " << events[j].event_id << " "
               << timestamp << " " << device_id << " " << motion_event_id_;
      }
      GetFrameData(events[j], device_id, data_key, timestamp, &content_);
      if (event_meta.event_id == vis_driven_event_id_) {
        // Init of frame_visualizer must be in one thread with render,
        // so you must move it from init_internal.
        if (!init_) {
          frame_visualizer_->init();
          // if (camera_visualizer_) {
          //     camera_visualizer_->init();
          // }
          init_ = true;
        }
        frame_visualizer_->update_camera_system(&content_);
        frame_visualizer_->render(&content_);
        //                frame_visualizer_->set_motion_buffer(motion_buffer_);
        // if (camera_visualizer_) {
        //     camera_visualizer_->render(content_);
        // }
      }
    }
  }
  return Status::OK();
}

REGISTER_SUBNODE(VisualizationSubnode);

}  // namespace perception
}  // namespace apollo
