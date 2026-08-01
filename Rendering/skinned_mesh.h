#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>

#include <fbxsdk.h>

#include <cereal/archives/binary.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/unordered_map.hpp>

// DirectXMathの単純な数値構造体をCerealで保存・復元できるようにする。
namespace DirectX
{
	template<class Archive>
	void serialize(Archive& archive, XMFLOAT2& value)
	{
		archive(
			cereal::make_nvp("x", value.x),
			cereal::make_nvp("y", value.y));
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT3& value)
	{
		archive(
			cereal::make_nvp("x", value.x),
			cereal::make_nvp("y", value.y),
			cereal::make_nvp("z", value.z));
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT4& value)
	{
		archive(
			cereal::make_nvp("x", value.x),
			cereal::make_nvp("y", value.y),
			cereal::make_nvp("z", value.z),
			cereal::make_nvp("w", value.w));
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT4X4& matrix)
	{
		archive(
			cereal::make_nvp("_11", matrix._11), cereal::make_nvp("_12", matrix._12),
			cereal::make_nvp("_13", matrix._13), cereal::make_nvp("_14", matrix._14),
			cereal::make_nvp("_21", matrix._21), cereal::make_nvp("_22", matrix._22),
			cereal::make_nvp("_23", matrix._23), cereal::make_nvp("_24", matrix._24),
			cereal::make_nvp("_31", matrix._31), cereal::make_nvp("_32", matrix._32),
			cereal::make_nvp("_33", matrix._33), cereal::make_nvp("_34", matrix._34),
			cereal::make_nvp("_41", matrix._41), cereal::make_nvp("_42", matrix._42),
			cereal::make_nvp("_43", matrix._43), cereal::make_nvp("_44", matrix._44));
	}
}

struct scene
{
	// FBXシーン内のノード1個分の情報を保持する。
	struct node
	{
		uint64_t unique_id{ 0 };
		std::string name;
		FbxNodeAttribute::EType attribute{ FbxNodeAttribute::eUnknown };
		int64_t parent_index{ -1 };

		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(unique_id, name, attribute, parent_index);
		}
	};

	// FBXの親子構造を、検索しやすい一次元配列として保持する。
	std::vector<node> nodes;

	template<class Archive>
	void serialize(Archive& archive)
	{
		archive(nodes);
	}

	// FBX固有IDからnodes配列内の位置を検索する。
	int64_t indexof(uint64_t unique_id) const
	{
		int64_t index{ 0 };

		for (const node& node : nodes)
		{
			if (node.unique_id == unique_id)
			{
				return index;
			}
			++index;
		}

		return -1;
	}
};

// FBXのスキンクラスターから取得した、メッシュのバインドポーズ情報。
struct skeleton
{
	struct bone
	{
		uint64_t unique_id{ 0 };
		std::string name;
		// bones配列内にある親ボーンの番号。親がいない場合は-1。
		int64_t parent_index{ -1 };
		// scene_view.nodes配列内にある、このボーンノードの番号。
		int64_t node_index{ 0 };

		// メッシュ空間の頂点を、バインド時のボーン空間へ移すオフセット行列。
		DirectX::XMFLOAT4X4 offset_transform{
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 };

		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(
				unique_id,
				name,
				parent_index,
				node_index,
				offset_transform);
		}

		bool is_orphan() const { return parent_index < 0; }
	};

	std::vector<bone> bones;

	template<class Archive>
	void serialize(Archive& archive)
	{
		archive(bones);
	}

	// FBX固有IDに対応するボーンの配列番号を返す。
	int64_t indexof(uint64_t unique_id) const
	{
		int64_t index = 0;
		for (const bone& bone : bones)
		{
			if (bone.unique_id == unique_id)
			{
				return index;
			}
			++index;
		}
		return -1;
	}
};

// FBXアニメーションを一定間隔でサンプリングしたキーフレーム列。
struct animation
{
	std::string name;
	float sampling_rate{ 0.0f };

	struct keyframe
	{
		struct node
		{
			// ノードのローカル座標からFBXシーンのグローバル座標へ変換する行列。
			DirectX::XMFLOAT4X4 global_transform{
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1 };

			// 親ノードを基準にしたローカル変換。実行時のアニメーション制御に使用する。
			DirectX::XMFLOAT3 scaling{ 1, 1, 1 };
			DirectX::XMFLOAT4 rotation{ 0, 0, 0, 1 }; // 回転クォータニオン
			DirectX::XMFLOAT3 translation{ 0, 0, 0 };

			template<class Archive>
			void serialize(Archive& archive)
			{
				archive(
					global_transform,
					scaling,
					rotation,
					translation);
			}
		};

		std::vector<node> nodes;

		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(nodes);
		}
	};

	std::vector<keyframe> sequence;

	template<class Archive>
	void serialize(Archive& archive)
	{
		archive(name, sampling_rate, sequence);
	}
};

class skinned_mesh
{
public:
	// Direct3Dの頂点入力へ渡す、1頂点あたりの最大ボーン影響数。
	static const int MAX_BONE_INFLUENCES{ 4 };
	// 1回の描画で頂点シェーダーへ渡せるボーン行列の最大数。
	static const int MAX_BONES{ 256 };

	struct vertex
	{
		// GPUへ渡す頂点位置・法線・接線・UV座標。
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal{ 0,1,0 };
		// xyzは接線方向、wは従法線の向きを復元する符号として使う。
		DirectX::XMFLOAT4 tangent{ 1,0,0,1 };
		DirectX::XMFLOAT2 texcoord{ 0,0 };

		// この頂点を変形させるボーン番号と、その影響度を組で保持する。
		// ボーンを持たないモデルは、既定のボーン0から100%影響を受けるものとして扱う。
		float bone_weights[MAX_BONE_INFLUENCES]{ 1, 0, 0, 0 };
		uint32_t bone_indices[MAX_BONE_INFLUENCES]{};

		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(
				position,
				normal,
				tangent,
				texcoord,
				bone_weights,
				bone_indices);
		}
	};

	struct constants
	{
		// オブジェクト単位で頂点シェーダーへ渡すデータ。
		DirectX::XMFLOAT4X4 world;
		DirectX::XMFLOAT4 material_color;
		// 各ボーンの変形行列。未設定時に頂点を変形しないよう先頭は単位行列にする。
		DirectX::XMFLOAT4X4 bone_transforms[MAX_BONES]{
			{ 1, 0, 0, 0,
			  0, 1, 0, 0,
			  0, 0, 1, 0,
			  0, 0, 0, 1 } };
	};

	struct mesh
	{
		// 同じメッシュ内で同一マテリアルを使う面をまとめた描画範囲。
		struct subset
		{
			uint64_t material_unique_id{ 0 };
			std::string material_name;

			// インデックスバッファ内の開始位置と描画する要素数。
			uint32_t start_index_location{ 0 };
			uint32_t index_count{ 0 };

			template<class Archive>
			void serialize(Archive& archive)
			{
				archive(
					material_unique_id,
					material_name,
					start_index_location,
					index_count);
			}
		};

		// FBXノードとメッシュを対応付けるための情報。
		uint64_t unique_id{ 0 };
		std::string name;
		// scene_view.nodesを参照するための配列番号。
		int64_t node_index{ 0 };

		// FBXシーン内で、このメッシュノードに設定されている位置・姿勢・拡大率。
		// 複数メッシュを、それぞれ作成時の場所へ配置するため描画時に使用する。
		DirectX::XMFLOAT4X4 default_global_transform{
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1 };

		// FBXから取得したCPU側の形状データ。
		std::vector<vertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<subset> subsets;
		// ローカル座標における境界ボックスの最小点と最大点。
		DirectX::XMFLOAT3 bounding_box[2]
		{
			{ +D3D11_FLOAT32_MAX, +D3D11_FLOAT32_MAX, +D3D11_FLOAT32_MAX },
			{ -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX }
		};
		// このメッシュを初期姿勢へ戻すためのボーン階層とオフセット行列。
		skeleton bind_pose;

		// GPUオブジェクトを除いた、FBX解析済みのCPUデータだけを保存する。
		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(
				unique_id,
				name,
				node_index,
				subsets,
				default_global_transform,
				bind_pose,
				bounding_box,
				vertices,
				indices);
		}

	private:
		// 描画時にDirect3Dへ設定するGPU側のバッファ。
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
		friend class skinned_mesh;
	};

	struct material
	{
		// FBXマテリアルの識別情報。
		uint64_t unique_id{ 0 };
		std::string name;

		// Ka:環境光、Kd:拡散反射、Ks:鏡面反射の色。
		DirectX::XMFLOAT4 Ka{ 0.2f, 0.2f, 0.2f, 1.0f };
		DirectX::XMFLOAT4 Kd{ 0.8f, 0.8f, 0.8f, 1.0f };
		DirectX::XMFLOAT4 Ks{ 1.0f, 1.0f, 1.0f, 1.0f };

		// テクスチャのファイル名と、GPU側の参照オブジェクト。
		std::string texture_filenames[4];
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
			shader_resource_views[4];

		template<class Archive>
		void serialize(Archive& archive)
		{
			archive(
				unique_id,
				name,
				Ka,
				Kd,
				Ks,
				texture_filenames);
		}
	};

private:

	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
	// 元FBXと同じ場所に作る、解析済みデータのキャッシュファイル名。
	std::filesystem::path cereal_filename;
	static constexpr uint32_t SERIALIZATION_VERSION{ 1 };
	bool load_serialized_data(const char* fbx_filename);
	bool save_serialized_data();

protected:

	// 読み込んだFBXのシーン・形状・材質を保持する。
	scene scene_view;
	std::vector<mesh> meshes;
	std::unordered_map<uint64_t, material> materials;

	// FBX SDKのオブジェクトから、描画に必要な情報を抽出する。
	void fetch_meshes(FbxScene* fbx_scene, std::vector<mesh>& meshes);
	void fetch_skeleton(FbxMesh* fbx_mesh, skeleton& bind_pose);
	void fetch_animations(
		FbxScene* fbx_scene,
		std::vector<animation>& animation_clips,
		float sampling_rate);
	void fetch_materials(
		FbxScene* fbx_scene,
		std::unordered_map<uint64_t, material>& materials);
	// 抽出した情報からDirect3Dのバッファやテクスチャを生成する。
	void create_com_objects(ID3D11Device* device, const char* fbx_filename);

public:
	// 読み込んだ全アニメーション。呼び出し側でクリップとフレームを選択する。
	std::vector<animation> animation_clips;

	skinned_mesh(
		ID3D11Device* device,
		const char* fbx_filename,
		bool triangulate = false,
		float sampling_rate = 0.0f);

	virtual ~skinned_mesh() = default;

	// 形状を持たない別FBXからアニメーションクリップだけを追加する。
	bool append_animations(
		const char* animation_filename,
		float sampling_rate = 0.0f);

	// 2つの姿勢のローカルSRTを補間し、結果をkeyframeへ格納する。
	void blend_animations(
		const animation::keyframe* keyframes[2],
		float factor,
		animation::keyframe& keyframe);

	// 編集されたローカル変換から、親子関係を反映したグローバル行列を再計算する。
	void update_animation(animation::keyframe& keyframe);

	void render(
		ID3D11DeviceContext* immediate_context,
		const DirectX::XMFLOAT4X4& world,
		const DirectX::XMFLOAT4& material_color,
		const animation::keyframe* keyframe = nullptr);
};
