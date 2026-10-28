#include "MapEditorSystem.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

transform_component map_editor_system::model_transform(const float position[3],float bounds_min_y,float yaw,float scale)
{
	transform_component transform;
	transform.scale=(std::max)(scale,0.001f);
	std::copy_n(position,3,transform.position);
	transform.position[1]-=bounds_min_y*transform.scale;
	transform.rotation[1]=yaw;
	std::fill_n(transform.scale3,3,transform.scale);
	return transform;
}

bool map_editor_system::make_ray(float mouse_x, float mouse_y, float view_x, float view_y,
	float view_width, float view_height, const XMFLOAT4X4& view,
	const XMFLOAT4X4& projection, map_editor_ray& ray) const
{
	if (view_width <= 1.0f || view_height <= 1.0f) return false;
	const float local_x = mouse_x - view_x;
	const float local_y = mouse_y - view_y;
	if (local_x < 0.0f || local_y < 0.0f || local_x > view_width || local_y > view_height) return false;

	// D3DのNDCはX/Yが-1～1、Zが0～1。Near/Farを逆変換してRayを作る。
	const float ndc_x = local_x / view_width * 2.0f - 1.0f;
	const float ndc_y = 1.0f - local_y / view_height * 2.0f;
	const XMMATRIX inverse_view_projection = XMMatrixInverse(nullptr,
		XMLoadFloat4x4(&view) * XMLoadFloat4x4(&projection));
	const XMVECTOR near_point = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 0.0f, 1.0f), inverse_view_projection);
	const XMVECTOR far_point = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f), inverse_view_projection);
	const XMVECTOR direction = XMVector3Normalize(far_point - near_point);
	XMFLOAT3 origin_value{}, direction_value{};
	XMStoreFloat3(&origin_value, near_point);
	XMStoreFloat3(&direction_value, direction);
	ray.origin[0] = origin_value.x; ray.origin[1] = origin_value.y; ray.origin[2] = origin_value.z;
	ray.direction[0] = direction_value.x; ray.direction[1] = direction_value.y; ray.direction[2] = direction_value.z;
	return true;
}

bool map_editor_system::intersect_plane(const map_editor_ray& ray, float height, float position[3]) const
{
	if (std::abs(ray.direction[1]) < 0.00001f) return false;
	const float distance = (height - ray.origin[1]) / ray.direction[1];
	if (distance < 0.0f) return false;
	for (int axis = 0; axis < 3; ++axis) position[axis] = ray.origin[axis] + ray.direction[axis] * distance;
	return true;
}

void map_editor_system::sanitize()
{
	auto step=[](float value,float fallback,float minimum,float maximum) {
		return std::isfinite(value) ? (std::clamp)(value,minimum,maximum) : fallback;
	};
	settings_.grid_size=step(settings_.grid_size,1.0f,0.01f,100.0f);
	settings_.rotation_step=step(settings_.rotation_step,15.0f,1.0f,180.0f);
	settings_.scale_step=step(settings_.scale_step,0.1f,0.01f,10.0f);
}

void map_editor_system::snap(float position[3],bool temporary) const
{
	if (!snapping(temporary)) return;
	const float grid = std::isfinite(settings_.grid_size) ? (std::max)(settings_.grid_size, 0.01f) : 1.0f;
	// 高さは床面やColliderのHit位置を保ち、XZだけを格子へ吸着する。
	position[0] = std::round(position[0] / grid) * grid;
	position[2] = std::round(position[2] / grid) * grid;
}
