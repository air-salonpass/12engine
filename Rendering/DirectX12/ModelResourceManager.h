#pragma once

#include "GltfModel.h"
#include "../Threading/AtomicJobWorker.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 複数glTF資産の共有、重複ロード防止、非同期ロード結果のフレーム境界公開を担当する。
//
// 【利用手順】request(path)で予約し、Main Threadの毎フレームupdate()で完了を回収し、
// find(path)が非nullになってからSceneへ公開する。ロード完了を待つwhileループは禁止。
// Workerはファイル解析を担当し、Scene Registryや描画中のCommand Listへ直接触れない。
class model_resource_manager12
{
public:
	void initialize(std::shared_ptr<directx12_device> device);
	void insert_loaded(const std::string& filename, std::shared_ptr<gltf_model12> model);
	bool request(const std::string& filename);
	std::vector<std::string> update();
	std::shared_ptr<gltf_model12> find(const std::string& filename) const;
	void stop();
	bool loading() const { return job_running_.load(std::memory_order_acquire) || !requests_.empty(); }
	const std::string& last_error() const { return last_error_; }

private:
	struct completed_load
	{
		std::string filename;
		std::shared_ptr<gltf_model12> model;
		std::string error;
	};
	void launch_next();
	static std::string normalized_path(const std::string& filename);

	std::shared_ptr<directx12_device> device_;
	atomic_job_worker worker_;
	std::unordered_map<std::string, std::shared_ptr<gltf_model12>> resources_;
	std::unordered_set<std::string> queued_;
	std::deque<std::string> requests_;
	std::mutex completed_mutex_;
	// filename/model/errorは一組の結果なのでatomicへ分割せず、mutexでまとめて公開する。
	std::deque<completed_load> completed_;
	// atomicはWorkerの制御状態だけに使う。GPU完了の同期にはD3D12 Fenceを使うこと。
	std::atomic<bool> job_running_{ false };
	std::string last_error_;
};
