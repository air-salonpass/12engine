#include "ModelResourceManager.h"

#include <filesystem>

void model_resource_manager12::initialize(std::shared_ptr<directx12_device> device)
{
	device_ = std::move(device);
}

std::string model_resource_manager12::normalized_path(const std::string& filename)
{
	if (filename.empty()) return {};
	const std::filesystem::path source(filename);
	// 日本語を含む作業フォルダをANSI文字列の絶対パスへ変換すると'?'へ欠損する。
	// Project内AssetはASCIIの相対パスを維持し、同一性だけlexically_normalで揃える。
	if (source.is_relative()) return source.lexically_normal().string();
	try
	{
		return std::filesystem::weakly_canonical(source).string();
	}
	catch (...)
	{
		return source.lexically_normal().string();
	}
}

void model_resource_manager12::insert_loaded(const std::string& filename, std::shared_ptr<gltf_model12> model)
{
	if (!model) return;
	const std::string key = normalized_path(filename);
	resources_[key] = std::move(model);
	queued_.erase(key);
}

bool model_resource_manager12::request(const std::string& filename)
{
	const std::string key = normalized_path(filename);
	if (!device_ || key.empty() || resources_.find(key) != resources_.end() || queued_.find(key) != queued_.end()) return false;
	queued_.insert(key);
	requests_.push_back(key);
	launch_next();
	return true;
}

void model_resource_manager12::launch_next()
{
	if (job_running_.load(std::memory_order_acquire) || requests_.empty()) return;
	const std::string filename = requests_.front();
	requests_.pop_front();
	job_running_.store(true, std::memory_order_release);
	const auto device = device_;
	if (!worker_.submit([this, device, filename]
	{
		completed_load result;
		result.filename = filename;
		try
		{
			result.model = std::make_shared<gltf_model12>(device);
			if (!result.model->load(filename))
			{
				result.error = result.model->error();
				result.model.reset();
			}
		}
		catch (const std::exception& exception) { result.error = exception.what(); result.model.reset(); }
		catch (...) { result.error = "モデル読込み中に不明なエラーが発生しました。"; result.model.reset(); }
		{
			std::lock_guard<std::mutex> lock(completed_mutex_);
			completed_.push_back(std::move(result));
		}
	}))
	{
		job_running_.store(false, std::memory_order_release);
		queued_.erase(filename);
	}
}

std::vector<std::string> model_resource_manager12::update()
{
	std::deque<completed_load> ready;
	{
		std::lock_guard<std::mutex> lock(completed_mutex_);
		ready.swap(completed_);
	}
	std::vector<std::string> loaded;
	if (!ready.empty()) job_running_.store(false, std::memory_order_release);
	for (completed_load& result : ready)
	{
		queued_.erase(result.filename);
		if (result.model)
		{
			resources_[result.filename] = std::move(result.model);
			loaded.push_back(result.filename);
			last_error_.clear();
		}
		else last_error_ = result.error.empty() ? "モデルの読込みに失敗しました。" : result.error;
	}
	launch_next();
	return loaded;
}

std::shared_ptr<gltf_model12> model_resource_manager12::find(const std::string& filename) const
{
	const auto found = resources_.find(normalized_path(filename));
	return found == resources_.end() ? nullptr : found->second;
}

void model_resource_manager12::stop()
{
	worker_.stop();
	job_running_.store(false, std::memory_order_release);
	requests_.clear();
}
