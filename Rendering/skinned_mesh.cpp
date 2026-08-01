#include "misc.h"
#include "skinned_mesh.h"
#include "shader.h"
#include "texture.h"

#include <sstream>
#include <functional>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <system_error>

using namespace DirectX;

namespace
{
	// 1つのコントロールポイントに対する、1本のボーンの番号と影響度。
	struct bone_influence
	{
		uint32_t bone_index{ 0 };
		float bone_weight{ 0.0f };
	};
	using bone_influences_per_control_point =
		std::vector<bone_influence>;

	// FBXのスキンクラスターを調べ、各コントロールポイントのボーン影響度を集める。
	void fetch_bone_influences(
		const FbxMesh* fbx_mesh,
		std::vector<bone_influences_per_control_point>& bone_influences)
	{
		const int control_points_count =
			fbx_mesh->GetControlPointsCount();
		bone_influences.clear();
		bone_influences.resize(control_points_count);

		uint32_t next_bone_index = 0;
		const int skin_count =
			fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
		for (int skin_index = 0; skin_index < skin_count; ++skin_index)
		{
			const FbxSkin* fbx_skin = static_cast<const FbxSkin*>(
				fbx_mesh->GetDeformer(skin_index, FbxDeformer::eSkin));
			if (!fbx_skin)
			{
				continue;
			}

			const int cluster_count = fbx_skin->GetClusterCount();
			for (int cluster_index = 0;
				cluster_index < cluster_count;
				++cluster_index, ++next_bone_index)
			{
				const FbxCluster* fbx_cluster =
					fbx_skin->GetCluster(cluster_index);
				if (!fbx_cluster)
				{
					continue;
				}

				const int influence_count =
					fbx_cluster->GetControlPointIndicesCount();
				for (int influence_index = 0;
					influence_index < influence_count;
					++influence_index)
				{
					const int control_point_index =
						fbx_cluster->GetControlPointIndices()[influence_index];
					const float bone_weight = static_cast<float>(
						fbx_cluster->GetControlPointWeights()[influence_index]);

					// 壊れた参照と影響度0のデータは頂点へ渡さない。
					if (control_point_index < 0 ||
						control_point_index >= control_points_count ||
						bone_weight <= 0.0f)
					{
						continue;
					}

					// 定数バッファーへ格納できる範囲のボーンだけを採用する。
					if (next_bone_index < skinned_mesh::MAX_BONES)
					{
						bone_influences.at(control_point_index).push_back(
							{ next_bone_index, bone_weight });
					}
				}
			}
		}

		// 5本以上から影響を受ける場合は、強い順の4本だけを残す。
		// 切り捨て後の合計が1になるよう正規化し、変形量が弱くなるのを防ぐ。
		for (bone_influences_per_control_point& influences : bone_influences)
		{
			std::sort(
				influences.begin(),
				influences.end(),
				[](const bone_influence& lhs, const bone_influence& rhs)
				{
					return lhs.bone_weight > rhs.bone_weight;
				});

			if (influences.size() > skinned_mesh::MAX_BONE_INFLUENCES)
			{
				influences.resize(skinned_mesh::MAX_BONE_INFLUENCES);
			}

			float total_weight = 0.0f;
			for (const bone_influence& influence : influences)
			{
				total_weight += influence.bone_weight;
			}
			if (total_weight > 0.0f)
			{
				for (bone_influence& influence : influences)
				{
					influence.bone_weight /= total_weight;
				}
			}
		}
	}

	// FBX SDKの倍精度4x4行列を、DirectXMathで扱う単精度行列へ変換する。
	inline XMFLOAT4X4 to_xmfloat4x4(const FbxAMatrix& fbx_matrix)
	{
		XMFLOAT4X4 matrix{};
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				matrix.m[row][column] =
					static_cast<float>(fbx_matrix[row][column]);
			}
		}
		return matrix;
	}

	// FBXの3要素ベクトルをDirectXMathのXMFLOAT3へ変換する。
	inline XMFLOAT3 to_xmfloat3(const FbxDouble3& fbx_vector)
	{
		return
		{
			static_cast<float>(fbx_vector[0]),
			static_cast<float>(fbx_vector[1]),
			static_cast<float>(fbx_vector[2])
		};
	}

	// FbxAMatrixの拡大率・移動が返す4要素ベクトルからxyzだけを取り出す。
	inline XMFLOAT3 to_xmfloat3(const FbxVector4& fbx_vector)
	{
		return
		{
			static_cast<float>(fbx_vector[0]),
			static_cast<float>(fbx_vector[1]),
			static_cast<float>(fbx_vector[2])
		};
	}

	// FBXの4要素ベクトルをDirectXMathのXMFLOAT4へ変換する。
	inline XMFLOAT4 to_xmfloat4(const FbxDouble4& fbx_vector)
	{
		return
		{
			static_cast<float>(fbx_vector[0]),
			static_cast<float>(fbx_vector[1]),
			static_cast<float>(fbx_vector[2]),
			static_cast<float>(fbx_vector[3])
		};
	}
}

skinned_mesh::skinned_mesh(ID3D11Device *device, const char *fbx_filename,
						   bool triangulate, float sampling_rate)
{
	// FBXと同じ場所に拡張子.cerealの解析済みキャッシュを用意する。
	cereal_filename = std::filesystem::u8path(fbx_filename);
	cereal_filename.replace_extension(".cereal");

	// 有効なキャッシュがあればFBX SDKによる重い解析を省略する。
	if (!load_serialized_data(fbx_filename))
	{
		// FBX SDK全体を管理するマネージャーと、読み込み先のシーンを作る。
		FbxManager *fbx_manager = FbxManager::Create();
		FbxScene *fbx_scene = FbxScene::Create(fbx_manager, "");

		// 指定されたFBXファイルをシーンへ読み込む。
		FbxImporter *fbx_importer = FbxImporter::Create(fbx_manager, "");

		bool import_status = false;

		import_status = fbx_importer->Initialize(fbx_filename);

		_ASSERT_EXPR_A(import_status,
					   fbx_importer->GetStatus().GetErrorString());

		import_status = fbx_importer->Import(fbx_scene);

		_ASSERT_EXPR_A(import_status,
					   fbx_importer->GetStatus().GetErrorString());

		// 必要に応じて多角形を三角形へ変換する。
		FbxGeometryConverter fbx_converter(fbx_manager);

		if (triangulate)
		{
			fbx_converter.Triangulate(fbx_scene, true, false);

			fbx_converter.RemoveBadPolygonsFromMeshes(fbx_scene);
		}

		// ルートから子ノードを再帰的にたどり、scene_viewへ格納する。
		std::function<void(FbxNode *)> traverse = [&](FbxNode *fbx_node)
		{
			scene::node &node = scene_view.nodes.emplace_back();

			node.attribute =
				fbx_node->GetNodeAttribute()
					? fbx_node->GetNodeAttribute()->GetAttributeType()
					: FbxNodeAttribute::eUnknown;

			node.name = fbx_node->GetName();

			node.unique_id = fbx_node->GetUniqueID();

			node.parent_index = scene_view.indexof(
				fbx_node->GetParent() ? fbx_node->GetParent()->GetUniqueID()
									  : 0);

			for (int child_index = 0; child_index < fbx_node->GetChildCount();
				 ++child_index)
			{
				traverse(fbx_node->GetChild(child_index));
			}
		};

		traverse(fbx_scene->GetRootNode());

		// FBX
		// SDKが保持している間に、形状・材質・アニメーションをアプリ側へ取り出す。
		fetch_meshes(fbx_scene, meshes);
		fetch_materials(fbx_scene, materials);
		fetch_animations(fbx_scene, animation_clips, sampling_rate);

		// マテリアルなしや不正な参照でも描画できるよう、ID=0の白色材質を常備する。
		if (materials.find(0) == materials.end())
		{
			materials.emplace(0, material{});
		}

#if 0

	for (const scene::node& node : scene_view.nodes)
	{
		FbxNode* fbx_node =
			fbx_scene->FindNodeByName(
				node.name.c_str());

		std::string node_name =
			fbx_node->GetName();

		uint64_t uid =
			fbx_node->GetUniqueID();

		uint64_t parent_uid =
			fbx_node->GetParent()
			?
			fbx_node->GetParent()->GetUniqueID()
			:
			0;

		int32_t type =
			fbx_node->GetNodeAttribute()
			?
			fbx_node->GetNodeAttribute()->GetAttributeType()
			:
			0;

		std::stringstream debug_string;

		debug_string
			<< node_name
			<< ":"
			<< uid
			<< ":"
			<< parent_uid
			<< ":"
			<< type
			<< "\n";

		OutputDebugStringA(
			debug_string.str().c_str());
	}

#endif

		// 抽出完了後はFBX SDK側の一時データをまとめて解放する。
		fbx_manager->Destroy();

		// 初回のFBX解析結果を保存し、次回以降の読み込みを高速化する。
		save_serialized_data();
	}

	// CPU側へ抽出した情報から、描画に使うGPUオブジェクトを作る。
	create_com_objects(device, fbx_filename);
}

bool skinned_mesh::load_serialized_data(const char *fbx_filename)
{
	std::error_code error;
	if (!std::filesystem::exists(cereal_filename, error) || error)
	{
		return false;
	}

	// 元FBXがキャッシュより新しい場合は、古い内容を使わず再解析する。
	const std::filesystem::path source_filename =
		std::filesystem::u8path(fbx_filename);
	const auto source_time =
		std::filesystem::last_write_time(source_filename, error);
	if (error)
	{
		return false;
	}
	const auto cache_time =
		std::filesystem::last_write_time(cereal_filename, error);
	if (error || cache_time < source_time)
	{
		return false;
	}

	try
	{
		std::ifstream input(cereal_filename, std::ios::binary);
		if (!input)
		{
			return false;
		}

		cereal::BinaryInputArchive archive(input);
		uint32_t version = 0;
		archive(version);
		if (version != SERIALIZATION_VERSION)
		{
			return false;
		}
		archive(scene_view, meshes, materials, animation_clips);
		return !scene_view.nodes.empty() && !meshes.empty();
	}
	catch (const cereal::Exception&)
	{
		// 途中書き込みや構造変更で読めない場合は、空に戻してFBXから復旧する。
		scene_view.nodes.clear();
		meshes.clear();
		materials.clear();
		animation_clips.clear();
		return false;
	}
}

bool skinned_mesh::save_serialized_data()
{
	try
	{
		// 一時ファイルへ完全に書き終えてから置換し、不完全なキャッシュを残さない。
		std::filesystem::path temporary_filename = cereal_filename;
		temporary_filename += ".tmp";
		{
			std::ofstream output(
				temporary_filename,
				std::ios::binary | std::ios::trunc);
			if (!output)
			{
				return false;
			}
			cereal::BinaryOutputArchive archive(output);
			uint32_t version = SERIALIZATION_VERSION;
			archive(version, scene_view, meshes, materials, animation_clips);
		}

		std::error_code error;
		std::filesystem::remove(cereal_filename, error);
		error.clear();
		std::filesystem::rename(
			temporary_filename,
			cereal_filename,
			error);
		return !error;
	}
	catch (const cereal::Exception&)
	{
		return false;
	}
}

void skinned_mesh::fetch_materials(
	FbxScene* fbx_scene,
	std::unordered_map<uint64_t, material>& materials)
{
	// マテリアルを持つ全ノードを調べ、色・拡散テクスチャ・法線マップを取得する。
	for (const scene::node& node : scene_view.nodes)
	{
		const FbxNode* fbx_node =
			fbx_scene->FindNodeByName(node.name.c_str());
		if (!fbx_node)
		{
			continue;
		}

		const int material_count = fbx_node->GetMaterialCount();
		for (int material_index = 0;
			material_index < material_count;
			++material_index)
		{
			const FbxSurfaceMaterial* fbx_material =
				fbx_node->GetMaterial(material_index);
			material material{};
			material.name = fbx_material->GetName();
			material.unique_id = fbx_material->GetUniqueID();

			// FBXの色プロパティをXMFLOAT4へ変換する共通処理。
			auto fetch_color =
				[fbx_material](const char* property_name, XMFLOAT4& destination)
				{
					FbxProperty property =
						fbx_material->FindProperty(property_name);
					if (property.IsValid())
					{
						const FbxDouble3 color = property.Get<FbxDouble3>();
						destination =
						{
							static_cast<float>(color[0]),
							static_cast<float>(color[1]),
							static_cast<float>(color[2]),
							1.0f
						};
					}
				};

			fetch_color(FbxSurfaceMaterial::sAmbient, material.Ka);
			fetch_color(FbxSurfaceMaterial::sDiffuse, material.Kd);
			fetch_color(FbxSurfaceMaterial::sSpecular, material.Ks);

			// Diffuseプロパティに接続されたテクスチャを取得する。
			FbxProperty diffuse_property =
				fbx_material->FindProperty(FbxSurfaceMaterial::sDiffuse);
			if (diffuse_property.IsValid())
			{
				const FbxFileTexture* fbx_texture =
					diffuse_property.GetSrcObject<FbxFileTexture>();
				if (fbx_texture)
				{
					material.texture_filenames[0] =
						fbx_texture->GetRelativeFileName();
				}
			}

			// NormalMapプロパティへ接続された法線マップを2枚目のスロットへ保存する。
			FbxProperty normal_map_property =
				fbx_material->FindProperty(FbxSurfaceMaterial::sNormalMap);
			if (normal_map_property.IsValid())
			{
				const FbxFileTexture* fbx_texture =
					normal_map_property.GetSrcObject<FbxFileTexture>();
				if (fbx_texture)
				{
					material.texture_filenames[1] =
						fbx_texture->GetRelativeFileName();
				}
			}

			materials.emplace(material.unique_id, std::move(material));
		}
	}
}

void skinned_mesh::fetch_meshes(
	FbxScene* fbx_scene,
	std::vector<mesh>& meshes)
{
	// メッシュ属性を持つノードだけを描画用データへ変換する。
	for (const scene::node& node : scene_view.nodes)
	{
		if (node.attribute != FbxNodeAttribute::eMesh)
		{
			continue;
		}

		FbxNode* fbx_node =
			fbx_scene->FindNodeByName(node.name.c_str());
		FbxMesh* fbx_mesh = fbx_node->GetMesh();

		mesh& mesh = meshes.emplace_back();
		mesh.unique_id = fbx_node->GetUniqueID();
		mesh.name = fbx_node->GetName();
		mesh.node_index = scene_view.indexof(mesh.unique_id);
		// メッシュごとのグローバル変換を保存し、複数メッシュの相対配置を保つ。
		mesh.default_global_transform =
			to_xmfloat4x4(fbx_node->EvaluateGlobalTransform());

		// FBXのコントロールポイント単位で、スキニング用のボーン影響度を取得する。
		std::vector<bone_influences_per_control_point> bone_influences;
		fetch_bone_influences(fbx_mesh, bone_influences);
		// 同じクラスター順でバインドポーズを取得し、頂点のボーン番号と対応させる。
		fetch_skeleton(fbx_mesh, mesh.bind_pose);

		// FBXノードに登録されたマテリアルごとにサブセットを作る。
		// マテリアルがない場合も、既定材質ID=0用の範囲を1つ用意する。
		std::vector<mesh::subset>& subsets = mesh.subsets;
		const int material_count = fbx_node->GetMaterialCount();
		subsets.resize(material_count > 0 ? material_count : 1);

		for (int material_index = 0;
			material_index < material_count;
			++material_index)
		{
			const FbxSurfaceMaterial* fbx_material =
				fbx_node->GetMaterial(material_index);
			subsets.at(material_index).material_name =
				fbx_material->GetName();
			subsets.at(material_index).material_unique_id =
				fbx_material->GetUniqueID();
		}

		// 1ポリゴンを3頂点として展開し、単純なインデックスを作る。
		const int polygon_count = fbx_mesh->GetPolygonCount();
		mesh.vertices.resize(static_cast<size_t>(polygon_count) * 3);
		mesh.indices.resize(static_cast<size_t>(polygon_count) * 3);

		// ポリゴンに割り当てられたFBXマテリアル番号を取得する。
		// eAllSameとeByPolygonの両方へ対応し、不正値は既定の0へ戻す。
		const FbxGeometryElementMaterial* material_element =
			fbx_mesh->GetElementMaterial();
		auto material_index_of =
			[material_count, material_element](int polygon_index)
			{
				if (material_count <= 0 || !material_element ||
					material_element->GetIndexArray().GetCount() <= 0)
				{
					return 0;
				}

				const int lookup_index =
					material_element->GetMappingMode() ==
					FbxGeometryElement::eAllSame ? 0 : polygon_index;
				if (lookup_index >= material_element->GetIndexArray().GetCount())
				{
					return 0;
				}

				const int material_index =
					material_element->GetIndexArray().GetAt(lookup_index);
				return material_index >= 0 && material_index < material_count
					? material_index : 0;
			};

		// 先に各サブセットの必要インデックス数を数え、開始位置を決める。
		for (int polygon_index = 0; polygon_index < polygon_count; ++polygon_index)
		{
			subsets.at(material_index_of(polygon_index)).index_count += 3;
		}

		uint32_t subset_offset = 0;
		for (mesh::subset& subset : subsets)
		{
			subset.start_index_location = subset_offset;
			subset_offset += subset.index_count;
			// 次の頂点展開処理では書き込み済み数として再利用する。
			subset.index_count = 0;
		}

		FbxStringList uv_names;
		fbx_mesh->GetUVSetNames(uv_names);
		const FbxVector4* control_points = fbx_mesh->GetControlPoints();

		// 法線マッピングに必要な接線がFBXにない場合は、先頭UVセットから生成する。
		if (fbx_mesh->GetElementTangentCount() == 0 &&
			fbx_mesh->GetElementUVCount() > 0)
		{
			fbx_mesh->GenerateTangentsData(0, false);
		}
		const FbxGeometryElementTangent* tangent_element =
			fbx_mesh->GetElementTangentCount() > 0
			? fbx_mesh->GetElementTangent(0)
			: nullptr;

		// FBXのマッピング方式と参照方式を解決し、現在の頂点に対応する接線を取得する。
		auto fetch_tangent =
			[tangent_element](
				int polygon_index,
				int position_in_polygon,
				int control_point_index,
				XMFLOAT4& tangent)
			{
				if (!tangent_element)
				{
					return;
				}

				int element_index = 0;
				switch (tangent_element->GetMappingMode())
				{
				case FbxGeometryElement::eByControlPoint:
					element_index = control_point_index;
					break;
				case FbxGeometryElement::eByPolygonVertex:
					element_index = polygon_index * 3 + position_in_polygon;
					break;
				case FbxGeometryElement::eByPolygon:
					element_index = polygon_index;
					break;
				case FbxGeometryElement::eAllSame:
					element_index = 0;
					break;
				default:
					return;
				}

				int direct_index = element_index;
				if (tangent_element->GetReferenceMode() ==
					FbxGeometryElement::eIndexToDirect)
				{
					if (element_index < 0 ||
						element_index >= tangent_element->GetIndexArray().GetCount())
					{
						return;
					}
					direct_index =
						tangent_element->GetIndexArray().GetAt(element_index);
				}
				if (direct_index < 0 ||
					direct_index >= tangent_element->GetDirectArray().GetCount())
				{
					return;
				}

				const FbxVector4 value =
					tangent_element->GetDirectArray().GetAt(direct_index);
				tangent =
				{
					static_cast<float>(value[0]),
					static_cast<float>(value[1]),
					static_cast<float>(value[2]),
					static_cast<float>(value[3])
				};
			};

		for (int polygon_index = 0; polygon_index < polygon_count; ++polygon_index)
		{
			_ASSERT_EXPR(
				fbx_mesh->GetPolygonSize(polygon_index) == 3,
				L"FBX mesh must be triangulated");

			// このポリゴンを、対応するマテリアルの連続領域へ配置する。
			mesh::subset& subset =
				subsets.at(material_index_of(polygon_index));
			const uint32_t index_offset =
				subset.start_index_location + subset.index_count;

			for (int position_in_polygon = 0; position_in_polygon < 3; ++position_in_polygon)
			{
				const int vertex_index =
					polygon_index * 3 + position_in_polygon;
				vertex vertex{};

				const int polygon_vertex =
					fbx_mesh->GetPolygonVertex(
						polygon_index,
						position_in_polygon);

				vertex.position.x =
					static_cast<float>(control_points[polygon_vertex][0]);
				vertex.position.y =
					static_cast<float>(control_points[polygon_vertex][1]);
				vertex.position.z =
					static_cast<float>(control_points[polygon_vertex][2]);

				// 同じコントロールポイントを参照する頂点へ、上位4本のボーン情報を複製する。
				const bone_influences_per_control_point& influences =
					bone_influences.at(polygon_vertex);
				for (size_t influence_index = 0;
					influence_index < influences.size();
					++influence_index)
				{
					vertex.bone_weights[influence_index] =
						influences.at(influence_index).bone_weight;
					vertex.bone_indices[influence_index] =
						influences.at(influence_index).bone_index;
				}

				// 法線が存在する場合は頂点単位の法線を取得する。
				if (fbx_mesh->GetElementNormalCount() > 0)
				{
					FbxVector4 normal;
					fbx_mesh->GetPolygonVertexNormal(
						polygon_index,
						position_in_polygon,
						normal);
					vertex.normal.x = static_cast<float>(normal[0]);
					vertex.normal.y = static_cast<float>(normal[1]);
					vertex.normal.z = static_cast<float>(normal[2]);
				}

				// UVが存在する場合はDirectX向けにV座標を上下反転する。
				if (fbx_mesh->GetElementUVCount() > 0 && uv_names.GetCount() > 0)
				{
					FbxVector2 uv;
					bool unmapped_uv = false;
					fbx_mesh->GetPolygonVertexUV(
						polygon_index,
						position_in_polygon,
						uv_names[0],
						uv,
						unmapped_uv);
					vertex.texcoord.x = static_cast<float>(uv[0]);
					vertex.texcoord.y = 1.0f - static_cast<float>(uv[1]);
				}

				// 接線xyzと従法線の向きを示す符号wを頂点へ保存する。
				fetch_tangent(
					polygon_index,
					position_in_polygon,
					polygon_vertex,
					vertex.tangent);

				mesh.vertices.at(vertex_index) = vertex;
				mesh.indices.at(
					static_cast<size_t>(index_offset) +
					position_in_polygon) =
					static_cast<uint32_t>(vertex_index);
				++subset.index_count;
			}
		}

		// 全頂点を走査し、モデル空間の境界ボックスを最小点・最大点として保持する。
		for (const vertex& vertex : mesh.vertices)
		{
			mesh.bounding_box[0].x = (std::min)(mesh.bounding_box[0].x, vertex.position.x);
			mesh.bounding_box[0].y = (std::min)(mesh.bounding_box[0].y, vertex.position.y);
			mesh.bounding_box[0].z = (std::min)(mesh.bounding_box[0].z, vertex.position.z);
			mesh.bounding_box[1].x = (std::max)(mesh.bounding_box[1].x, vertex.position.x);
			mesh.bounding_box[1].y = (std::max)(mesh.bounding_box[1].y, vertex.position.y);
			mesh.bounding_box[1].z = (std::max)(mesh.bounding_box[1].z, vertex.position.z);
		}
	}
}

void skinned_mesh::fetch_animations(
	FbxScene* fbx_scene,
	std::vector<animation>& animation_clips,
	float sampling_rate)
{
	FbxArray<FbxString*> animation_stack_names;
	fbx_scene->FillAnimStackNameArray(animation_stack_names);

	const int animation_stack_count = animation_stack_names.GetCount();
	for (int stack_index = 0;
		stack_index < animation_stack_count;
		++stack_index)
	{
		if (!animation_stack_names[stack_index])
		{
			continue;
		}

		animation clip{};
		clip.name = animation_stack_names[stack_index]->Buffer();
		FbxAnimStack* animation_stack =
			fbx_scene->FindMember<FbxAnimStack>(clip.name.c_str());
		if (!animation_stack)
		{
			continue;
		}
		fbx_scene->SetCurrentAnimationStack(animation_stack);

		// 0以下が指定された場合は、FBXシーン本来のフレームレートを使用する。
		const FbxTime::EMode time_mode =
			fbx_scene->GetGlobalSettings().GetTimeMode();
		const double default_frame_rate = FbxTime::GetFrameRate(time_mode);
		clip.sampling_rate = sampling_rate > 0.0f
			? sampling_rate
			: static_cast<float>(default_frame_rate);
		if (clip.sampling_rate <= 0.0f)
		{
			clip.sampling_rate = 30.0f;
		}

		FbxTime one_second;
		one_second.SetSecondDouble(1.0);
		FbxTime sampling_interval;
		FbxLongLong sampling_interval_ticks =
			static_cast<FbxLongLong>(
				one_second.Get() / clip.sampling_rate);
		if (sampling_interval_ticks <= 0)
		{
			sampling_interval_ticks = 1;
		}
		sampling_interval.Set(sampling_interval_ticks);

		const FbxTakeInfo* take_info =
			fbx_scene->GetTakeInfo(clip.name.c_str());
		const FbxTimeSpan time_span = take_info
			? take_info->mLocalTimeSpan
			: animation_stack->GetLocalTimeSpan();
		const FbxTime start_time = time_span.GetStart();
		const FbxTime stop_time = time_span.GetStop();

		// 各サンプル時刻で全ノードのグローバル変換を保存する。
		for (FbxTime time = start_time;
			time <= stop_time;
			time += sampling_interval)
		{
			animation::keyframe& keyframe = clip.sequence.emplace_back();
			keyframe.nodes.resize(scene_view.nodes.size());

			for (size_t node_index = 0;
				node_index < scene_view.nodes.size();
				++node_index)
			{
				FbxNode* fbx_node = fbx_scene->FindNodeByName(
					scene_view.nodes.at(node_index).name.c_str());
				if (fbx_node)
				{
					animation::keyframe::node& node =
						keyframe.nodes.at(node_index);
					node.global_transform =
						to_xmfloat4x4(
							fbx_node->EvaluateGlobalTransform(time));

					// 親ノードを基準にしたローカルSRTも保存し、後から個別編集できるようにする。
					const FbxAMatrix local_transform =
						fbx_node->EvaluateLocalTransform(time);
					node.scaling = to_xmfloat3(local_transform.GetS());
					node.rotation = to_xmfloat4(local_transform.GetQ());
					node.translation = to_xmfloat3(local_transform.GetT());
				}
			}
		}

		if (!clip.sequence.empty())
		{
			animation_clips.emplace_back(std::move(clip));
		}
	}

	// FillAnimStackNameArrayが生成した文字列は呼び出し側で解放する。
	for (int stack_index = 0;
		stack_index < animation_stack_count;
		++stack_index)
	{
		delete animation_stack_names[stack_index];
	}
}

bool skinned_mesh::append_animations(
	const char* animation_filename,
	float sampling_rate)
{
	// 追加FBXは形状を作らず、アニメーションを抽出する間だけFBX SDK上へ展開する。
	FbxManager* fbx_manager = FbxManager::Create();
	if (!fbx_manager)
	{
		return false;
	}

	FbxScene* fbx_scene = FbxScene::Create(fbx_manager, "");
	FbxImporter* fbx_importer = FbxImporter::Create(fbx_manager, "");
	if (!fbx_scene || !fbx_importer)
	{
		fbx_manager->Destroy();
		return false;
	}

	bool import_status = fbx_importer->Initialize(animation_filename);
	_ASSERT_EXPR_A(
		import_status,
		fbx_importer->GetStatus().GetErrorString());
	if (import_status)
	{
		import_status = fbx_importer->Import(fbx_scene);
		_ASSERT_EXPR_A(
			import_status,
			fbx_importer->GetStatus().GetErrorString());
	}

	if (import_status)
	{
		std::vector<animation> imported_clips;
		fetch_animations(fbx_scene, imported_clips, sampling_rate);

		// 同名クリップは置換し、キャッシュ読込後に同じFBXを追加しても重複させない。
		for (animation& imported_clip : imported_clips)
		{
			auto existing_clip = std::find_if(
				animation_clips.begin(),
				animation_clips.end(),
				[&imported_clip](const animation& clip)
				{
					return clip.name == imported_clip.name;
				});
			if (existing_clip != animation_clips.end())
			{
				*existing_clip = std::move(imported_clip);
			}
			else
			{
				animation_clips.emplace_back(std::move(imported_clip));
			}
		}

		// 追加後のクリップも次回のキャッシュ読込対象に含める。
		save_serialized_data();
	}

	fbx_manager->Destroy();
	return import_status;
}

void skinned_mesh::blend_animations(
	const animation::keyframe* keyframes[2],
	float factor,
	animation::keyframe& keyframe)
{
	if (!keyframes[0] || !keyframes[1])
	{
		keyframe.nodes.clear();
		return;
	}

	// 同じスケルトンのキーフレーム同士を前提とし、安全のため短い方の要素数に合わせる。
	const size_t node_count = (std::min)(
		keyframes[0]->nodes.size(),
		keyframes[1]->nodes.size());
	keyframe.nodes.resize(node_count);

	for (size_t node_index = 0; node_index < node_count; ++node_index)
	{
		const animation::keyframe::node& source0 =
			keyframes[0]->nodes.at(node_index);
		const animation::keyframe::node& source1 =
			keyframes[1]->nodes.at(node_index);
		animation::keyframe::node& destination =
			keyframe.nodes.at(node_index);

		// 拡大率と移動量は線形補間する。
		XMStoreFloat3(
			&destination.scaling,
			XMVectorLerp(
				XMLoadFloat3(&source0.scaling),
				XMLoadFloat3(&source1.scaling),
				factor));
		XMStoreFloat3(
			&destination.translation,
			XMVectorLerp(
				XMLoadFloat3(&source0.translation),
				XMLoadFloat3(&source1.translation),
				factor));

		// 回転は球面線形補間を使い、2姿勢を自然な回転経路でつなぐ。
		XMStoreFloat4(
			&destination.rotation,
			XMQuaternionNormalize(
				XMQuaternionSlerp(
					XMQuaternionNormalize(XMLoadFloat4(&source0.rotation)),
					XMQuaternionNormalize(XMLoadFloat4(&source1.rotation)),
					factor)));
	}
}

void skinned_mesh::update_animation(animation::keyframe& keyframe)
{
	const size_t node_count = (std::min)(
		keyframe.nodes.size(),
		scene_view.nodes.size());

	// scene_viewは親を先に登録した順序なので、先頭から計算すれば子へ最新行列を渡せる。
	for (size_t node_index = 0; node_index < node_count; ++node_index)
	{
		animation::keyframe::node& node = keyframe.nodes.at(node_index);
		const XMMATRIX scaling = XMMatrixScaling(
			node.scaling.x,
			node.scaling.y,
			node.scaling.z);
		const XMMATRIX rotation = XMMatrixRotationQuaternion(
			XMQuaternionNormalize(XMLoadFloat4(&node.rotation)));
		const XMMATRIX translation = XMMatrixTranslation(
			node.translation.x,
			node.translation.y,
			node.translation.z);

		const int64_t parent_index =
			scene_view.nodes.at(node_index).parent_index;
		XMMATRIX parent_transform = XMMatrixIdentity();
		if (parent_index >= 0 &&
			static_cast<size_t>(parent_index) < keyframe.nodes.size())
		{
			parent_transform = XMLoadFloat4x4(
				&keyframe.nodes.at(static_cast<size_t>(parent_index)).global_transform);
		}

		XMStoreFloat4x4(
			&node.global_transform,
			scaling * rotation * translation * parent_transform);
	}
}

void skinned_mesh::fetch_skeleton(
	FbxMesh* fbx_mesh,
	skeleton& bind_pose)
{
	bind_pose.bones.clear();
	std::vector<FbxCluster*> clusters;

	// 複数のスキンデフォーマーがある場合も、クラスターを出現順に連結する。
	// この順番はfetch_bone_influencesで割り当てるボーン番号と一致する。
	const int deformer_count =
		fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int deformer_index = 0;
		deformer_index < deformer_count;
		++deformer_index)
	{
		FbxSkin* skin = static_cast<FbxSkin*>(
			fbx_mesh->GetDeformer(deformer_index, FbxDeformer::eSkin));
		if (!skin)
		{
			continue;
		}

		for (int cluster_index = 0;
			cluster_index < skin->GetClusterCount() &&
			clusters.size() < MAX_BONES;
			++cluster_index)
		{
			FbxCluster* cluster = skin->GetCluster(cluster_index);
			if (cluster && cluster->GetLink())
			{
				clusters.push_back(cluster);
			}
		}
	}

	// 先に全ボーンのIDを登録し、配列の前後に関係なく親を検索できるようにする。
	bind_pose.bones.resize(clusters.size());
	for (size_t bone_index = 0; bone_index < clusters.size(); ++bone_index)
	{
		FbxNode* bone_node = clusters.at(bone_index)->GetLink();
		skeleton::bone& bone = bind_pose.bones.at(bone_index);
		bone.name = bone_node->GetName();
		bone.unique_id = bone_node->GetUniqueID();
		bone.node_index = scene_view.indexof(bone.unique_id);
	}

	for (size_t bone_index = 0; bone_index < clusters.size(); ++bone_index)
	{
		FbxCluster* cluster = clusters.at(bone_index);
		FbxNode* bone_node = cluster->GetLink();
		skeleton::bone& bone = bind_pose.bones.at(bone_index);
		bone.parent_index = bone_node->GetParent()
			? bind_pose.indexof(bone_node->GetParent()->GetUniqueID())
			: -1;

		// メッシュ空間からシーン空間へ移す初期行列。
		FbxAMatrix reference_global_init_position;
		cluster->GetTransformMatrix(reference_global_init_position);
		// ボーン空間からシーン空間へ移す、バインド時の初期行列。
		FbxAMatrix cluster_global_init_position;
		cluster->GetTransformLinkMatrix(cluster_global_init_position);

		// ボーン初期行列の逆行列を掛け、メッシュ空間からボーン空間へ変換する。
		bone.offset_transform = to_xmfloat4x4(
			cluster_global_init_position.Inverse() *
			reference_global_init_position);
	}
}

void skinned_mesh::create_com_objects(
	ID3D11Device* device,
	const char* fbx_filename)
{
	// 各メッシュの頂点・インデックスをGPUバッファへ転送する。
	for (mesh& mesh : meshes)
	{
		D3D11_BUFFER_DESC buffer_desc{};
		D3D11_SUBRESOURCE_DATA subresource_data{};

		buffer_desc.ByteWidth =
			static_cast<UINT>(sizeof(vertex) * mesh.vertices.size());
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		subresource_data.pSysMem = mesh.vertices.data();

		HRESULT hr = device->CreateBuffer(
			&buffer_desc,
			&subresource_data,
			mesh.vertex_buffer.ReleaseAndGetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

		buffer_desc.ByteWidth =
			static_cast<UINT>(sizeof(uint32_t) * mesh.indices.size());
		buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		subresource_data.pSysMem = mesh.indices.data();

		hr = device->CreateBuffer(
			&buffer_desc,
			&subresource_data,
			mesh.index_buffer.ReleaseAndGetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

		// 頂点・インデックスは追加アニメーション後にキャッシュを更新できるようCPU側にも残す。
	}

	// 拡散テクスチャと法線マップを読み込み、未指定なら用途別のダミーを作る。
	for (auto& entry : materials)
	{
		material& material = entry.second;
		for (size_t texture_index = 0; texture_index < 2; ++texture_index)
		{
			if (!material.texture_filenames[texture_index].empty())
			{
				std::filesystem::path texture_path(fbx_filename);
				texture_path.replace_filename(
					std::filesystem::u8path(
						material.texture_filenames[texture_index]));

				D3D11_TEXTURE2D_DESC texture2d_desc{};
				load_texture_from_file(
					device,
					texture_path.c_str(),
					material.shader_resource_views[texture_index].GetAddressOf(),
					&texture2d_desc);
			}
			else
			{
				// 法線マップなしでは(0.5, 0.5, 1.0)を使い、表面法線を変化させない。
				const uint32_t dummy_color = texture_index == 1
					? 0xFFFF7F7F
					: 0xFFFFFFFF;
				make_dummy_texture(
					device,
					material.shader_resource_views[texture_index].GetAddressOf(),
					dummy_color,
					16);
			}
		}
	}

	// vertex構造体と頂点シェーダー入力の対応を定義する。
	D3D11_INPUT_ELEMENT_DESC input_element_desc[]
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
		{ "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
		{ "BONES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0,
			D3D11_APPEND_ALIGNED_ELEMENT },
	};

	create_vs_from_cso(
		device,
		"skinned_mesh_vs.cso",
		vertex_shader.ReleaseAndGetAddressOf(),
		input_layout.ReleaseAndGetAddressOf(),
		input_element_desc,
		ARRAYSIZE(input_element_desc));

	create_ps_from_cso(
		device,
		"skinned_mesh_ps.cso",
		pixel_shader.ReleaseAndGetAddressOf());

	// ワールド行列と材質色を渡す定数バッファを作る。
	D3D11_BUFFER_DESC buffer_desc{};
	buffer_desc.ByteWidth = sizeof(constants);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	HRESULT hr = device->CreateBuffer(
		&buffer_desc,
		nullptr,
		constant_buffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}

void skinned_mesh::render(
	ID3D11DeviceContext* immediate_context,
	const XMFLOAT4X4& world,
	const XMFLOAT4& material_color,
	const animation::keyframe* keyframe)
{
	// 登録されたメッシュを順番にDirect3Dへ設定して描画する。
	for (const mesh& mesh : meshes)
	{
		UINT stride = sizeof(vertex);
		UINT offset = 0;

		immediate_context->IASetVertexBuffers(
			0,
			1,
			mesh.vertex_buffer.GetAddressOf(),
			&stride,
			&offset);
		immediate_context->IASetIndexBuffer(
			mesh.index_buffer.Get(),
			DXGI_FORMAT_R32_UINT,
			0);
		immediate_context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		immediate_context->IASetInputLayout(input_layout.Get());

		immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);

		// アニメーション中はメッシュノード自身の時間変化する位置・姿勢を使用する。
		// キーフレームがない場合は、FBX読み込み時の初期配置へフォールバックする。
		constants data{};
		XMMATRIX mesh_transform =
			XMLoadFloat4x4(&mesh.default_global_transform);
		if (keyframe && mesh.node_index >= 0 &&
			static_cast<size_t>(mesh.node_index) < keyframe->nodes.size())
		{
			const animation::keyframe::node& mesh_node =
				keyframe->nodes.at(static_cast<size_t>(mesh.node_index));
			mesh_transform = XMLoadFloat4x4(&mesh_node.global_transform);
		}
		XMStoreFloat4x4(
			&data.world,
			mesh_transform * XMLoadFloat4x4(&world));

		// 実際のアニメーション行列を読み込むまでは、全ボーンを単位行列にする。
		// 配列の先頭以外も初期化し、ボーン1以降の頂点が原点へ潰れるのを防ぐ。
		for (XMFLOAT4X4& bone_transform : data.bone_transforms)
		{
			XMStoreFloat4x4(&bone_transform, XMMatrixIdentity());
		}

		// 指定キーフレームのグローバル姿勢を、メッシュ空間用のボーン行列へ変換する。
		if (keyframe)
		{
			const size_t bone_count = (std::min)(
				mesh.bind_pose.bones.size(),
				static_cast<size_t>(MAX_BONES));
			// ボーンのシーン座標を、同じ時刻のメッシュ座標へ戻してから適用する。
			const XMMATRIX inverse_mesh_transform =
				XMMatrixInverse(nullptr, mesh_transform);

			for (size_t bone_index = 0;
				bone_index < bone_count;
				++bone_index)
			{
				const skeleton::bone& bone =
					mesh.bind_pose.bones.at(bone_index);
				if (bone.node_index < 0 ||
					static_cast<size_t>(bone.node_index) >= keyframe->nodes.size())
				{
					continue;
				}

				const animation::keyframe::node& bone_node =
					keyframe->nodes.at(static_cast<size_t>(bone.node_index));
				XMStoreFloat4x4(
					&data.bone_transforms[bone_index],
					XMLoadFloat4x4(&bone.offset_transform) *
					XMLoadFloat4x4(&bone_node.global_transform) *
					inverse_mesh_transform);
			}
		}

		// UNIT23動作確認用。1へ変更するとボーン1と2を逆方向へ45度回転させる。
		// 変形確認後は0へ戻し、通常状態では単位行列を使用する。
#if 0
		XMStoreFloat4x4(
			&data.bone_transforms[1],
			XMMatrixRotationRollPitchYaw(
				0, 0, XMConvertToRadians(+45.0f)));
		XMStoreFloat4x4(
			&data.bone_transforms[2],
			XMMatrixRotationRollPitchYaw(
				0, 0, XMConvertToRadians(-45.0f)));
#endif

		// UNIT24動作確認用。バインドポーズのオフセット行列と、
		// 親子階層を考慮したダミーポーズを合成する。確認後は0へ戻す。
#if 0
		if (mesh.bind_pose.bones.size() >= 3)
		{
			XMMATRIX bind_transform[3]
			{
				XMLoadFloat4x4(&mesh.bind_pose.bones.at(0).offset_transform),
				XMLoadFloat4x4(&mesh.bind_pose.bones.at(1).offset_transform),
				XMLoadFloat4x4(&mesh.bind_pose.bones.at(2).offset_transform)
			};

			XMMATRIX animation_transform[3];
			animation_transform[0] = XMMatrixRotationRollPitchYaw(
				XMConvertToRadians(90.0f), 0, 0);
			animation_transform[1] = XMMatrixRotationRollPitchYaw(
				0, 0, XMConvertToRadians(+45.0f)) *
				XMMatrixTranslation(0, 2, 0);
			animation_transform[2] = XMMatrixRotationRollPitchYaw(
				0, 0, XMConvertToRadians(-45.0f)) *
				XMMatrixTranslation(0, 2, 0);

			XMStoreFloat4x4(
				&data.bone_transforms[0],
				bind_transform[0] * animation_transform[0]);
			XMStoreFloat4x4(
				&data.bone_transforms[1],
				bind_transform[1] * animation_transform[1] *
				animation_transform[0]);
			XMStoreFloat4x4(
				&data.bone_transforms[2],
				bind_transform[2] * animation_transform[2] *
				animation_transform[1] * animation_transform[0]);
		}
#endif

		// サブセットごとに対応する材質色・テクスチャへ切り替えて描画する。
		for (const mesh::subset& subset : mesh.subsets)
		{
			// サブセットが参照するマテリアルを取得する。
			// 不正なIDの場合はマテリアルなし用の既定材質へフォールバックする。
			auto material_iterator =
				materials.find(subset.material_unique_id);
			if (material_iterator == materials.end())
			{
				material_iterator = materials.find(0);
			}
			_ASSERT_EXPR(
				material_iterator != materials.end(),
				L"Material referenced by subset was not found");

			const material& material = material_iterator->second;
			XMStoreFloat4(
				&data.material_color,
				XMLoadFloat4(&material_color) *
				XMLoadFloat4(&material.Kd));

			immediate_context->UpdateSubresource(
				constant_buffer.Get(),
				0,
				nullptr,
				&data,
				0,
				0);
			immediate_context->VSSetConstantBuffers(
				0,
				1,
				constant_buffer.GetAddressOf());

			// 拡散テクスチャをt0、法線マップをt1へ設定する。
			immediate_context->PSSetShaderResources(
				0,
				1,
				material.shader_resource_views[0].GetAddressOf());
			immediate_context->PSSetShaderResources(
				1,
				1,
				material.shader_resource_views[1].GetAddressOf());

			immediate_context->DrawIndexed(
				subset.index_count,
				subset.start_index_location,
				0);
		}
	}
}
