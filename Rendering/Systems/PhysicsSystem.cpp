#include "PhysicsSystem.h"

#include "../Components/CharacterControllerComponent.h"
#include "../Components/ColliderComponent.h"
#include "../Components/RigidBodyComponent.h"
#include "../Components/TransformComponent.h"
#include "../Scene/SceneRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
	struct vector3
	{
		float x = 0, y = 0, z = 0;
	};

	vector3 operator+(vector3 a, vector3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	vector3 operator-(vector3 a, vector3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	vector3 operator*(vector3 value, float scale) { return { value.x * scale, value.y * scale, value.z * scale }; }
	float dot(vector3 a, vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	float length_squared(vector3 value) { return dot(value, value); }
	vector3 normalized(vector3 value)
	{
		const float length = std::sqrt(length_squared(value));
		return length > 0.00001f ? value * (1.0f / length) : vector3{ 0, 1, 0 };
	}

	struct world_collider
	{
		scene_entity entity = null_scene_entity;
		transform_component* transform = nullptr;
		collider_component* collider = nullptr;
		rigid_body_component* body = nullptr;
		character_controller_component* controller = nullptr;
		vector3 center{};
		vector3 half{};
		float radius = 0.5f;
	};

	float inverse_mass(const world_collider& value)
	{
		if (value.controller && value.controller->enabled) return 1.0f;
		if (!value.body || !value.body->enabled || value.body->type != rigid_body_type::dynamic_body) return 0.0f;
		return 1.0f / (std::max)(value.body->mass, 0.001f);
	}

	vector3 velocity(const world_collider& value)
	{
		if (value.controller && value.controller->enabled)
			return { value.controller->desired_velocity[0], value.controller->vertical_velocity,
				value.controller->desired_velocity[2] };
		if (value.body) return { value.body->velocity[0], value.body->velocity[1], value.body->velocity[2] };
		return {};
	}

	void set_velocity(world_collider& value, vector3 velocity)
	{
		if (value.controller && value.controller->enabled)
		{
			value.controller->desired_velocity[0] = velocity.x;
			value.controller->vertical_velocity = velocity.y;
			value.controller->desired_velocity[2] = velocity.z;
		}
		else if (value.body)
		{
			value.body->velocity[0] = value.body->freeze_position[0] ? 0.0f : velocity.x;
			value.body->velocity[1] = value.body->freeze_position[1] ? 0.0f : velocity.y;
			value.body->velocity[2] = value.body->freeze_position[2] ? 0.0f : velocity.z;
		}
	}

	bool layers_collide(const collider_component& first, const collider_component& second)
	{
		const std::uint32_t first_bit = 1u << (std::min)(first.layer, 31u);
		const std::uint32_t second_bit = 1u << (std::min)(second.layer, 31u);
		return (first.collision_mask & second_bit) != 0 && (second.collision_mask & first_bit) != 0;
	}

	bool box_box(const world_collider& a, const world_collider& b, vector3& normal, float& penetration)
	{
		const vector3 difference = b.center - a.center;
		const float overlap_x = a.half.x + b.half.x - std::abs(difference.x);
		const float overlap_y = a.half.y + b.half.y - std::abs(difference.y);
		const float overlap_z = a.half.z + b.half.z - std::abs(difference.z);
		if (overlap_x <= 0 || overlap_y <= 0 || overlap_z <= 0) return false;
		penetration = overlap_x;
		normal = { difference.x < 0 ? -1.0f : 1.0f, 0, 0 };
		if (overlap_y < penetration) { penetration = overlap_y; normal = { 0, difference.y < 0 ? -1.0f : 1.0f, 0 }; }
		if (overlap_z < penetration) { penetration = overlap_z; normal = { 0, 0, difference.z < 0 ? -1.0f : 1.0f }; }
		return true;
	}

	bool sphere_sphere(const world_collider& a, const world_collider& b, vector3& normal, float& penetration)
	{
		const vector3 difference = b.center - a.center;
		const float radius = a.radius + b.radius;
		const float distance_squared = length_squared(difference);
		if (distance_squared >= radius * radius) return false;
		const float distance = std::sqrt((std::max)(distance_squared, 0.000001f));
		normal = distance_squared > 0.000001f ? difference * (1.0f / distance) : vector3{ 0, 1, 0 };
		penetration = radius - distance;
		return true;
	}

	bool sphere_box(const world_collider& sphere, const world_collider& box, vector3& normal, float& penetration)
	{
		const vector3 minimum = box.center - box.half;
		const vector3 maximum = box.center + box.half;
		const vector3 closest{
			(std::clamp)(sphere.center.x, minimum.x, maximum.x),
			(std::clamp)(sphere.center.y, minimum.y, maximum.y),
			(std::clamp)(sphere.center.z, minimum.z, maximum.z) };
		const vector3 toward_box = closest - sphere.center;
		const float distance_squared = length_squared(toward_box);
		if (distance_squared >= sphere.radius * sphere.radius) return false;
		if (distance_squared > 0.000001f)
		{
			const float distance = std::sqrt(distance_squared);
			normal = toward_box * (1.0f / distance);
			penetration = sphere.radius - distance;
			return true;
		}
		// Sphere中心がBox内の場合は、中心間の最短軸を使って外へ押し出す。
		world_collider point_box = box;
		world_collider point_sphere = sphere;
		point_sphere.half = { sphere.radius, sphere.radius, sphere.radius };
		return box_box(point_sphere, point_box, normal, penetration);
	}

	bool collide(const world_collider& a, const world_collider& b, vector3& normal, float& penetration)
	{
		const bool a_sphere = a.collider->shape == collider_shape::sphere;
		const bool b_sphere = b.collider->shape == collider_shape::sphere;
		if (a_sphere && b_sphere) return sphere_sphere(a, b, normal, penetration);
		if (a_sphere && !b_sphere) return sphere_box(a, b, normal, penetration);
		if (!a_sphere && b_sphere)
		{
			if (!sphere_box(b, a, normal, penetration)) return false;
			normal = normal * -1.0f;
			return true;
		}
		// Capsuleは縦長AABBとして扱う。回転Capsuleと段差処理はCharacter Controller拡張で追加する。
		return box_box(a, b, normal, penetration);
	}

	bool ray_sphere(vector3 origin, vector3 direction, const world_collider& sphere,
		float max_distance, float& distance, vector3& normal)
	{
		const vector3 offset = origin - sphere.center;
		const float b = dot(offset, direction);
		const float c = dot(offset, offset) - sphere.radius * sphere.radius;
		const float discriminant = b * b - c;
		if (discriminant < 0.0f) return false;
		distance = -b - std::sqrt(discriminant);
		if (distance < 0.0f) distance = -b + std::sqrt(discriminant);
		if (distance < 0.0f || distance > max_distance) return false;
		normal = normalized(origin + direction * distance - sphere.center);
		return true;
	}

	bool ray_box(vector3 origin, vector3 direction, const world_collider& box,
		float max_distance, float& distance, vector3& normal)
	{
		const float origins[3]{ origin.x, origin.y, origin.z };
		const float directions[3]{ direction.x, direction.y, direction.z };
		const float centers[3]{ box.center.x, box.center.y, box.center.z };
		const float halves[3]{ box.half.x, box.half.y, box.half.z };
		float minimum_time = 0.0f;
		float maximum_time = max_distance;
		int normal_axis = 1;
		float normal_sign = 1.0f;
		for (int axis = 0; axis < 3; ++axis)
		{
			const float minimum = centers[axis] - halves[axis];
			const float maximum = centers[axis] + halves[axis];
			if (std::abs(directions[axis]) < 0.000001f)
			{
				if (origins[axis] < minimum || origins[axis] > maximum) return false;
				continue;
			}
			float near_time = (minimum - origins[axis]) / directions[axis];
			float far_time = (maximum - origins[axis]) / directions[axis];
			float near_normal = directions[axis] > 0.0f ? -1.0f : 1.0f;
			if (near_time > far_time) std::swap(near_time, far_time);
			if (near_time > minimum_time)
			{
				minimum_time = near_time;
				normal_axis = axis;
				normal_sign = near_normal;
			}
			maximum_time = (std::min)(maximum_time, far_time);
			if (minimum_time > maximum_time) return false;
		}
		distance = minimum_time;
		normal = {};
		if (normal_axis == 0) normal.x = normal_sign;
		else if (normal_axis == 1) normal.y = normal_sign;
		else normal.z = normal_sign;
		return distance <= max_distance;
	}

	world_collider make_world_collider(scene_entity entity, transform_component& transform,
		collider_component& collider)
	{
		const float scale_x = std::abs(transform.scale3[0]);
		const float scale_y = std::abs(transform.scale3[1]);
		const float scale_z = std::abs(transform.scale3[2]);
		world_collider world{ entity, &transform, &collider };
		world.center = { transform.position[0] + collider.center[0] * scale_x,
			transform.position[1] + collider.center[1] * scale_y,
			transform.position[2] + collider.center[2] * scale_z };
		if (collider.shape == collider_shape::sphere)
		{
			world.radius = collider.radius * (std::max)({ scale_x, scale_y, scale_z });
			world.half = { world.radius, world.radius, world.radius };
		}
		else if (collider.shape == collider_shape::capsule)
		{
			world.radius = collider.radius * (std::max)(scale_x, scale_z);
			world.half = { world.radius, (std::max)(collider.height * scale_y * 0.5f, world.radius), world.radius };
		}
		else world.half = { collider.half_extents[0] * scale_x,
			collider.half_extents[1] * scale_y, collider.half_extents[2] * scale_z };
		return world;
	}
}

bool physics_system::raycast(scene_registry& scene, const float origin_value[3], const float direction_value[3],
	float max_distance, unsigned int layer_mask, physics_raycast_hit& hit, scene_entity ignored_entity) const
{
	const vector3 origin{ origin_value[0], origin_value[1], origin_value[2] };
	const vector3 direction = normalized({ direction_value[0], direction_value[1], direction_value[2] });
	float nearest = (std::max)(max_distance, 0.0f);
	bool found = false;
	scene.for_each<transform_component, collider_component>([&](scene_entity entity,
		transform_component& transform, collider_component& collider)
	{
		if (entity == ignored_entity) return;
		if (!collider.enabled || (layer_mask & (1u << (std::min)(collider.layer, 31u))) == 0) return;
		const world_collider world = make_world_collider(entity, transform, collider);
		float distance = 0.0f;
		vector3 normal{};
		const bool intersects = collider.shape == collider_shape::sphere ?
			ray_sphere(origin, direction, world, nearest, distance, normal) :
			ray_box(origin, direction, world, nearest, distance, normal);
		if (!intersects || distance > nearest) return;
		nearest = distance;
		found = true;
		hit.entity = entity;
		hit.distance = distance;
		const vector3 point = origin + direction * distance;
		hit.point[0] = point.x; hit.point[1] = point.y; hit.point[2] = point.z;
		hit.normal[0] = normal.x; hit.normal[1] = normal.y; hit.normal[2] = normal.z;
	});
	return found;
}

std::vector<scene_entity> physics_system::overlap_sphere(scene_registry& scene,
	const float center_value[3], float radius, unsigned int layer_mask) const
{
	std::vector<scene_entity> result;
	const vector3 center{ center_value[0], center_value[1], center_value[2] };
	radius = (std::max)(radius, 0.0f);
	scene.for_each<transform_component, collider_component>([&](scene_entity entity,
		transform_component& transform, collider_component& collider)
	{
		if (!collider.enabled || (layer_mask & (1u << (std::min)(collider.layer, 31u))) == 0) return;
		const world_collider world = make_world_collider(entity, transform, collider);
		if (collider.shape == collider_shape::sphere)
		{
			const float combined = radius + world.radius;
			if (length_squared(world.center - center) <= combined * combined) result.push_back(entity);
			return;
		}
		const vector3 minimum = world.center - world.half;
		const vector3 maximum = world.center + world.half;
		const vector3 closest{ (std::clamp)(center.x, minimum.x, maximum.x),
			(std::clamp)(center.y, minimum.y, maximum.y), (std::clamp)(center.z, minimum.z, maximum.z) };
		if (length_squared(closest - center) <= radius * radius) result.push_back(entity);
	});
	return result;
}

void physics_system::update(scene_registry& scene, float elapsed_seconds)
{
	// 可変deltaを直接使うとFPSによって衝突結果が変わるため、60Hz固定Stepへ蓄積する。
	constexpr float fixed_delta = 1.0f / 60.0f;
	accumulator_ += (std::clamp)(elapsed_seconds, 0.0f, 0.1f);
	int steps = 0;
	while (accumulator_ >= fixed_delta && steps < 4)
	{
		// 複数Stepを処理した場合も、公開する接触一覧は最後の物理状態だけにする。
		// 途中Stepですでに離れたPairを誤ってStay扱いしないために毎Step再構築する。
		contacts_.clear();
		simulate(scene, fixed_delta);
		accumulator_ -= fixed_delta;
		++steps;
	}
	// Fixed Stepが実行されなかったFrameでは、直前の接触状態を維持する。
	// Render FPSがPhysics FPSより高い場合に誤ったExitを発生させないためである。
	if (steps > 0) rebuild_events();
	else events_.clear();
	// 長時間停止後に追いつこうとしてMain Threadを占有し続けない。
	if (steps == 4) accumulator_ = 0.0f;
}

void physics_system::clear()
{
	accumulator_ = 0.0f;
	contacts_.clear();
	events_.clear();
	previous_contacts_.clear();
}

std::uint64_t physics_system::make_contact_key(scene_entity first, scene_entity second) noexcept
{
	if (first > second) std::swap(first, second);
	return (static_cast<std::uint64_t>(first) << 32u) | static_cast<std::uint64_t>(second);
}

void physics_system::rebuild_events()
{
	std::unordered_map<std::uint64_t, physics_contact> current;
	for (physics_contact contact : contacts_)
	{
		// Pairの並びを固定すると、Broad Phaseの並び順が変化してもStayとして追跡できる。
		if (contact.first > contact.second)
		{
			std::swap(contact.first, contact.second);
			contact.normal[0] *= -1.0f;
			contact.normal[1] *= -1.0f;
			contact.normal[2] *= -1.0f;
		}
		current[make_contact_key(contact.first, contact.second)] = contact;
	}

	contacts_.clear();
	events_.clear();
	contacts_.reserve(current.size());
	events_.reserve(current.size() + previous_contacts_.size());
	for (const auto& [key, contact] : current)
	{
		contacts_.push_back(contact);
		const physics_event_phase phase = previous_contacts_.find(key) == previous_contacts_.end() ?
			physics_event_phase::enter : physics_event_phase::stay;
		events_.push_back({ contact, phase });
	}
	for (const auto& [key, previous] : previous_contacts_)
	{
		if (current.find(key) != current.end()) continue;
		physics_contact ended = previous;
		ended.penetration = 0.0f;
		events_.push_back({ ended, physics_event_phase::exit });
	}
	previous_contacts_ = std::move(current);
}

std::vector<physics_event> physics_system::events_for(scene_entity entity) const
{
	std::vector<physics_event> result;
	for (const physics_event& event : events_)
		if (event.contact.first == entity || event.contact.second == entity) result.push_back(event);
	return result;
}

void physics_system::simulate(scene_registry& scene, float delta)
{
	std::vector<world_collider> colliders;
	scene.for_each<transform_component, collider_component>(
		[&](scene_entity entity, transform_component& transform, collider_component& collider)
		{
			if (!collider.enabled) return;
			auto* body = scene.get<rigid_body_component>(entity);
			auto* controller = scene.get<character_controller_component>(entity);
			if (controller && controller->enabled)
			{
				// Controllerは全Colliderの境界が揃った後で移動する。段差上と頭上を同じ状態で調べるため。
			}
			else if (body && body->enabled && body->type != rigid_body_type::static_body)
			{
				if (body->type == rigid_body_type::dynamic_body) body->velocity[1] -= 9.80665f * body->gravity_scale * delta;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (body->freeze_position[axis]) body->velocity[axis] = 0.0f;
					else transform.position[axis] += body->velocity[axis] * delta;
				}
			}
			const float scale_x = std::abs(transform.scale3[0]);
			const float scale_y = std::abs(transform.scale3[1]);
			const float scale_z = std::abs(transform.scale3[2]);
			world_collider world{ entity, &transform, &collider, body, controller };
			world.center = { transform.position[0] + collider.center[0] * scale_x,
				transform.position[1] + collider.center[1] * scale_y,
				transform.position[2] + collider.center[2] * scale_z };
			if (collider.shape == collider_shape::sphere)
			{
				world.radius = collider.radius * (std::max)({ scale_x, scale_y, scale_z });
				world.half = { world.radius, world.radius, world.radius };
			}
			else if (collider.shape == collider_shape::capsule)
			{
				world.radius = collider.radius * (std::max)(scale_x, scale_z);
				world.half = { world.radius, (std::max)(collider.height * scale_y * 0.5f, world.radius), world.radius };
			}
			else world.half = { collider.half_extents[0] * scale_x,
				collider.half_extents[1] * scale_y, collider.half_extents[2] * scale_z };
			colliders.push_back(world);
		});

	for (auto& player : colliders)
	{
		auto* controller=player.controller;
		if (!controller || !controller->enabled || player.collider->trigger) continue;
		const bool was_grounded=controller->grounded;
		const bool can_step=was_grounded && controller->vertical_velocity<=0;
		controller->grounded=false;
		controller->vertical_velocity-=controller->gravity*delta;
		const vector3 original=player.center;
		const world_collider* support_surface=nullptr;
		player.center=original+vector3{controller->desired_velocity[0]*delta,controller->vertical_velocity*delta,controller->desired_velocity[2]*delta};
		const float height=(std::clamp)(controller->step_height,0.0f,2.0f);
		auto solid=[&](const world_collider& other) {
			return other.entity!=player.entity && !other.collider->trigger && layers_collide(*player.collider,*other.collider);
		};
		auto terrain=[&](const world_collider& other) {
			return solid(other) && !(other.controller && other.controller->enabled) &&
				(!other.body || !other.body->enabled || other.body->type==rigid_body_type::static_body) && other.collider->shape==collider_shape::box;
		};
		if (can_step && height>0 && player.collider->shape!=collider_shape::sphere)
		{
			const float old_bottom=original.y-player.half.y;
			float step_top=old_bottom;
			for (const auto& other:colliders)
			{
				if (!terrain(other)) continue;
				vector3 normal{}; float penetration=0;
				const float top=other.center.y+other.half.y;
				if (top>old_bottom+0.01f && top<=old_bottom+height+0.001f && collide(player,other,normal,penetration))
					if (top>step_top) { step_top=top; support_surface=&other; }
			}
			if (step_top>old_bottom)
			{
				world_collider lifted=player; lifted.center.y=step_top+player.half.y;
				world_collider lift_start=lifted; lift_start.center.x=original.x; lift_start.center.z=original.z;
				// 持ち上げた位置と移動先に頭上の空間がある場合だけ登る。高い壁・低い天井を貫通しない。
				bool clear=true;
				for (const auto& other:colliders) if (solid(other))
				{
					vector3 normal{}; float penetration=0;
					if ((collide(lifted,other,normal,penetration) && penetration>0.0001f) ||
						(collide(lift_start,other,normal,penetration) && penetration>0.0001f)) { clear=false; break; }
				}
				if (clear) { player.center=lifted.center; controller->grounded=true; controller->vertical_velocity=0; }
				else support_surface=nullptr;
			}
			// 下り階段では小さな高さ差だけ接地面へ吸着する。ジャンプ中や橋からの大きな落下には使わない。
			if (!controller->grounded)
			{
				const float bottom=player.center.y-player.half.y;
				float support=bottom-height-0.01f; bool found=false;
				for (const auto& other:colliders) if (terrain(other))
				{
					const float top=other.center.y+other.half.y;
					if (std::abs(player.center.x-other.center.x)<player.half.x+other.half.x-0.001f &&
						std::abs(player.center.z-other.center.z)<player.half.z+other.half.z-0.001f &&
						top<=old_bottom+0.001f && top>=bottom-height && top>support) { support=top; found=true; support_surface=&other; }
				}
				if (found) { player.center.y=support+player.half.y; controller->grounded=true; controller->vertical_velocity=0; }
			}
		}
		const vector3 movement=player.center-original;
		player.transform->position[0]+=movement.x; player.transform->position[1]+=movement.y; player.transform->position[2]+=movement.z;
		// 吸着でめり込みを解消した接地も接触イベントへ残す。着地中に床のExitが交互に出ることを防ぐ。
		if (controller->grounded && support_surface)
			contacts_.push_back({player.entity,support_surface->entity,{0,-1,0},0,false});
	}

	// X軸の最小座標で並べ、区間が離れた時点で内側Loopを終了する。
	// 全組合せO(n^2)をそのままNarrow Phaseへ渡さず、広いMapで静的物体が増えた場合の負荷を抑える。
	std::sort(colliders.begin(), colliders.end(), [](const world_collider& left, const world_collider& right)
	{
		return left.center.x - left.half.x < right.center.x - right.half.x;
	});
	for (std::size_t first = 0; first < colliders.size(); ++first)
	{
		const float first_maximum_x = colliders[first].center.x + colliders[first].half.x;
		for (std::size_t second = first + 1; second < colliders.size(); ++second)
		{
			auto& a = colliders[first];
			auto& b = colliders[second];
			if (b.center.x - b.half.x > first_maximum_x) break;
			if (!layers_collide(*a.collider, *b.collider)) continue;
			vector3 normal{};
			float penetration = 0.0f;
			if (!collide(a, b, normal, penetration)) continue;
			contacts_.push_back({ a.entity, b.entity, {normal.x, normal.y, normal.z}, penetration,
				a.collider->trigger || b.collider->trigger });
			if (a.collider->trigger || b.collider->trigger) continue;
			const float inverse_a = inverse_mass(a);
			const float inverse_b = inverse_mass(b);
			const float inverse_total = inverse_a + inverse_b;
			if (inverse_total <= 0.0f) continue;

			// 深くめり込む前に位置を補正する。normalはAからBを向く。
			const vector3 correction = normal * (penetration / inverse_total);
			if (inverse_a > 0.0f)
			{
				a.transform->position[0] -= correction.x * inverse_a;
				a.transform->position[1] -= correction.y * inverse_a;
				a.transform->position[2] -= correction.z * inverse_a;
				a.center=a.center-correction*inverse_a;
			}
			if (inverse_b > 0.0f)
			{
				b.transform->position[0] += correction.x * inverse_b;
				b.transform->position[1] += correction.y * inverse_b;
				b.transform->position[2] += correction.z * inverse_b;
				b.center=b.center+correction*inverse_b;
			}

			vector3 velocity_a = velocity(a);
			vector3 velocity_b = velocity(b);
			const float separating_velocity = dot(velocity_b - velocity_a, normal);
			if (separating_velocity < 0.0f)
			{
				const float restitution = (std::min)(a.body ? a.body->restitution : 0.0f,
					b.body ? b.body->restitution : 0.0f);
				const float impulse_size = -(1.0f + restitution) * separating_velocity / inverse_total;
				velocity_a = velocity_a - normal * (impulse_size * inverse_a);
				velocity_b = velocity_b + normal * (impulse_size * inverse_b);
				// 接線方向の速度を摩擦Impulseで減らす。静止摩擦を完全再現するSolverではないが、
				// 床上の物体が永遠に滑り続ける状態を防ぐ。
				const vector3 relative_after_normal = velocity_b - velocity_a;
				const vector3 tangent_value = relative_after_normal - normal * dot(relative_after_normal, normal);
				if (length_squared(tangent_value) > 0.000001f)
				{
					const vector3 tangent = normalized(tangent_value);
					float friction_impulse = -dot(relative_after_normal, tangent) / inverse_total;
					const float friction = std::sqrt((std::max)(0.0f, a.body ? a.body->friction : 0.6f) *
						(std::max)(0.0f, b.body ? b.body->friction : 0.6f));
					friction_impulse = (std::clamp)(friction_impulse, -impulse_size * friction, impulse_size * friction);
					velocity_a = velocity_a - tangent * (friction_impulse * inverse_a);
					velocity_b = velocity_b + tangent * (friction_impulse * inverse_b);
				}
				set_velocity(a, velocity_a);
				set_velocity(b, velocity_b);
			}
			if (a.controller && normal.y < -0.5f) { a.controller->grounded = true; a.controller->vertical_velocity = 0.0f; }
			if (b.controller && normal.y > 0.5f) { b.controller->grounded = true; b.controller->vertical_velocity = 0.0f; }
		}
	}
}
