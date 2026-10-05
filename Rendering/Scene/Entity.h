#pragma once

#include <cstdint>

// Scene内のオブジェクトを識別する軽量ID。0は未選択を表す予約値とする。
using scene_entity = std::uint32_t;
constexpr scene_entity null_scene_entity = 0;
