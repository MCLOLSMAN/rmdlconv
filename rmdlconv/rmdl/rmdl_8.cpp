#include "stdafx.h"
#include "versions.h"
#include "rmem.h"
#include "BinaryIO.h"
#include "rmdl/studio_rmdl.h"
#include "mdl/studio.h"

#include <map>

/*
	Type:    RMDL
	Version: 8
	Game:    Apex Legends Seasons 0-1

	Files: .rmdl, .vtx, .vvd, .vvc, .vvw
*/

void CreateVGFile_v8(const std::string& filePath)
{
	printf("creating VG file from v8 rmdl...\n");

	std::string rmdlPath = ChangeExtension(filePath, "rmdl");
	std::string vtxPath = ChangeExtension(filePath, "vtx");
	std::string vvdPath = ChangeExtension(filePath, "vvd");
	std::string vvcPath = ChangeExtension(filePath, "vvc");
	std::string vvwPath = ChangeExtension(filePath, "vvw");

	if (!FILE_EXISTS(vtxPath) || !FILE_EXISTS(vvdPath))
		Error("failed to convert external model components (vtx, vvd) to VG. '.vtx' and '.vvd' files are required but were not found\n");

	char* rmdlBuf = nullptr;
	{
		uintmax_t rmdlSize = GetFileSize(rmdlPath);

		rmdlBuf = new char[rmdlSize];

		std::ifstream ifs(rmdlPath, std::ios::in | std::ios::binary);

		ifs.read(rmdlBuf, rmdlSize);
	}

	char* vtxBuf = nullptr;
	{
		uintmax_t vtxSize = GetFileSize(vtxPath);

		vtxBuf = new char[vtxSize];

		std::ifstream ifs(vtxPath, std::ios::in | std::ios::binary);

		ifs.read(vtxBuf, vtxSize);
	}

	char* vvdBuf = nullptr;
	{
		uintmax_t vvdSize = GetFileSize(vvdPath);

		vvdBuf = new char[vvdSize];

		std::ifstream ifs(vvdPath, std::ios::in | std::ios::binary);

		ifs.read(vvdBuf, vvdSize);
	}

	// vertex color and uv2 buffer
	vertexColorFileHeader_t* vvc = nullptr;

	if (FILE_EXISTS(vvcPath))
	{
		char* vvcBuf = nullptr;
		{
			uintmax_t vvcSize = GetFileSize(vvcPath);

			vvcBuf = new char[vvcSize];

			std::ifstream ifs(vvcPath, std::ios::in | std::ios::binary);

			ifs.read(vvcBuf, vvcSize);
		}

		vvc = reinterpret_cast<vertexColorFileHeader_t*>(vvcBuf);
	}

	// extended weight buffer
	vertexWeightFileHeader_t* vvw = nullptr;

	if (FILE_EXISTS(vvwPath))
	{
		char* vvwBuf = nullptr;
		{
			uintmax_t vvwSize = GetFileSize(vvwPath);

			vvwBuf = new char[vvwSize];

			std::ifstream ifs(vvwPath, std::ios::in | std::ios::binary);

			ifs.read(vvwBuf, vvwSize);
		}

		vvw = reinterpret_cast<vertexWeightFileHeader_t*>(vvwBuf);
	}

	r5::v8::studiohdr_t * rmdl = reinterpret_cast<r5::v8::studiohdr_t*>(rmdlBuf);
	FileHeader_t* vtx = reinterpret_cast<FileHeader_t*>(vtxBuf);
	vertexFileHeader_t* vvd = reinterpret_cast<vertexFileHeader_t*>(vvdBuf);

	if ((rmdl->flags & STUDIOHDR_FLAGS_USES_VERTEX_COLOR) || (rmdl->flags & STUDIOHDR_FLAGS_USES_UV2)  && !FILE_EXISTS(vvcPath))
		Error("model requires 'vvc' file but could not be found \n");

	if (!FILE_EXISTS(vvwPath) && (rmdl->flags & STUDIOHDR_FLAGS_COMPLEX_WEIGHTS))
		Error("model requires 'vvw' file but could not be found \n");

	int numVertices = 0;
	int numMeshes = 0;
	int numLODs = vtx->numLODs;
	int vertCacheSize = 0;

	std::vector<ModelLODHeader_VG_t> lods;
	std::vector<uint16_t> indices;
	std::vector<StripHeader_t> strips;
	std::vector<Vertex_VG_t> vertices;
	std::vector<mstudioboneweight_t> legacyWeights; // we can't just use these so obviously a hold over from vvd
	std::vector<mstudioexternalweight_t> externalWeights;
	std::vector<MeshHeader_VG_t> meshes;
	std::vector<uint8_t> BoneStates;
	std::map<uint8_t, uint8_t> boneMap;

	int boneMapIdx = 0;
	short externalWeightIdx = 0;
	int vtxVertOffset = 0;

	for (int k = 0; k < vtx->numLODs; ++k)
	{
		vtxVertOffset = 0;

		for (int i = 0; i < vtx->numBodyParts; ++i)
		{
			BodyPartHeader_t* bodyPart = vtx->bodyPart(i);
			mstudiobodyparts_t* rmdlBodyPart = rmdl->bodypart(i);

			for (int j = 0; j < bodyPart->numModels; ++j)
			{
				ModelHeader_t* model = bodyPart->model(j);
				mstudiomodel_t_v54* rmdlModel = rmdlBodyPart->model(j);

				ModelLODHeader_t* lod = model->lod(k);

				lods.push_back(ModelLODHeader_VG_t{ (short)numMeshes, (short)lod->numMeshes, lod->switchPoint });

				numMeshes += lod->numMeshes;

				for (int l = 0; l < lod->numMeshes; ++l)
				{
					vertCacheSize = 0;
					externalWeightIdx = 0; // reset index for new mesh

					MeshHeader_t* mesh = lod->mesh(l);
					mstudiomesh_t_v54* rmdlMesh = rmdlModel->mesh(l);

					MeshHeader_VG_t newMesh{};

					newMesh.stripOffset = strips.size();
					newMesh.indexOffset = indices.size();
					newMesh.externalWeightOffset = externalWeights.size() * sizeof(mstudioexternalweight_t);
					newMesh.legacyWeightOffset = numVertices;
					newMesh.flags = 0x2005A40; // hard coded packed weights and pos

					// ideally later we add an option because some models actually use unpacked pos in vg
					newMesh.flags |= VG_PACKED_POSITION;

					if (newMesh.flags & VG_PACKED_POSITION)
						vertCacheSize += sizeof(Vector64);
					else
						vertCacheSize += sizeof(Vector3);

					vertCacheSize += sizeof(mstudiopackedboneweight_t) + sizeof(uint32_t) + sizeof(Vector2); // packed weight size, packed normal size, uv size

					if (rmdl->flags & STUDIOHDR_FLAGS_USES_UV2)
					{
						newMesh.flags |= VG_UV_LAYER2;
						vertCacheSize += sizeof(Vector2);
					}

					if (rmdl->flags & STUDIOHDR_FLAGS_USES_VERTEX_COLOR)
					{
						newMesh.flags |= VG_VERTEX_COLOR;
						vertCacheSize += sizeof(VertexColor_t);
					}

					//newMesh.flags += 0x1; // set unpacked pos flag
					//newMesh.flags += 0x5000; // set packed weight flag

					newMesh.vertCacheSize = vertCacheSize;
					newMesh.vertOffset = numVertices * vertCacheSize;

					for (int m = 0; m < mesh->numStripGroups; ++m)
					{
						StripGroupHeader_t* stripGroup = mesh->stripGroup(m);
						//mstudiomesh_t_v54* rmdlMesh = rmdlMeshes.at(m);

						int prevTotalVerts = numVertices;
						numVertices += stripGroup->numVerts;

						newMesh.numVerts += stripGroup->numVerts;
						newMesh.numStrips += stripGroup->numStrips;
						newMesh.numIndices += stripGroup->numIndices;
						newMesh.numLegacyWeights += stripGroup->numVerts;

						int lastVertId = -1;
						for (int v = 0; v < stripGroup->numVerts; ++v)
						{
							Vertex_t* vertVtx = stripGroup->vert(v);

							mstudiovertex_t* vertVvd = vvd->vertex(vtxVertOffset + vertVtx->origMeshVertID);
							Vector4* tangentVvd = vvd->tangent(vtxVertOffset + vertVtx->origMeshVertID);

							//printf("vertex %i \n", vtxVertOffset + vertVtx->origMeshVertID);

							Vector4 newTangent{};
							newTangent.x = tangentVvd->x;
							newTangent.y = tangentVvd->y;
							newTangent.z = tangentVvd->z;
							newTangent.w = tangentVvd->w;

							Vertex_VG_t newVert{};
							newVert.m_NormalTangentPacked = PackNormalTangent_UINT32(vertVvd->m_vecNormal, newTangent);
							newVert.m_vecPositionPacked = PackPos_UINT64(vertVvd->m_vecPosition);
							newVert.m_vecPosition = vertVvd->m_vecPosition;
							newVert.m_vecTexCoord = vertVvd->m_vecTexCoord;

							for (int n = 0; n < vertVvd->m_BoneWeights.numbones; n++)
							{
								if (n < 3 && !boneMap.count(vertVvd->m_BoneWeights.bone[n]))
								{
									boneMap.insert(std::pair<uint8_t, uint8_t>(vertVvd->m_BoneWeights.bone[n], boneMapIdx));
									printf("added bone %i at idx %i\n", boneMap.find(vertVvd->m_BoneWeights.bone[n])->first, boneMap.find(vertVvd->m_BoneWeights.bone[n])->second);
									boneMapIdx++;

									BoneStates.push_back(boneMap.find(vertVvd->m_BoneWeights.bone[n])->first);
								}
								else if (n >= 3) // don't try to read extended weights if below this amount, will cause issues
								{
									mstudioexternalweight_t* externalWeight = vvw->weight(vertVvd->m_BoneWeights.weights.packedweight.externalweightindex + (n - 3));

									if (!boneMap.count(externalWeight->bone))
									{
										boneMap.insert(std::pair<uint8_t, uint8_t>(externalWeight->bone, boneMapIdx));
										printf("added bone %i at idx %i\n", boneMap.find(externalWeight->bone)->first, boneMap.find(externalWeight->bone)->second);
										boneMapIdx++;

										BoneStates.push_back(boneMap.find(externalWeight->bone)->first);
									}
								}
							}

							// set the actual weights
							if (rmdl->flags & STUDIOHDR_FLAGS_COMPLEX_WEIGHTS)
							{								
								// "complex" weights
								newVert.m_BoneWeightsPacked.weight[1] = externalWeightIdx; // set this before so we can add for the next one

								for (int n = 0; n < vertVvd->m_BoneWeights.numbones; n++)
								{
									// this causes the weird conversion artifact that normal vg has where verts with one weight have the same bone in two slots
									// so obviously respawn is doing something similar
									if (n == (vertVvd->m_BoneWeights.numbones - 1))
									{
										if (n > 0 && n < 3)
										{
											newVert.m_BoneWeightsPacked.bone[1] = boneMap.find(vertVvd->m_BoneWeights.bone[n])->second;
										}
										else if (n > 2)
										{
											mstudioexternalweight_t* vvwWeight = vvw->weight(vertVvd->m_BoneWeights.weights.packedweight.externalweightindex + (n - 3)); // subtract three to get the real idx

											newVert.m_BoneWeightsPacked.bone[1] = boneMap.find(vvwWeight->bone)->second; // change this to the bone remap
										}
									}
									else
									{
										if (n > 0 && n < 3) // we have to "build" this external weight so we do funny thing
										{
											mstudioexternalweight_t newExternalWeight{};

											newExternalWeight.weight = vertVvd->m_BoneWeights.weights.packedweight.weight[n];
											newExternalWeight.bone = boneMap.find(vertVvd->m_BoneWeights.bone[n])->second;

											externalWeights.push_back(newExternalWeight);

											externalWeightIdx++;
										}
										else if (n > 2)
										{
											mstudioexternalweight_t* vvwWeight = vvw->weight(vertVvd->m_BoneWeights.weights.packedweight.externalweightindex + (n - 3));

											mstudioexternalweight_t newExternalWeight{};

											newExternalWeight.weight = vvwWeight->weight;
											newExternalWeight.bone = boneMap.find(vvwWeight->bone)->second; // change this to the bone remap

											externalWeights.push_back(newExternalWeight);

											externalWeightIdx++;
										}
									}
								}

								// first slot is fixed, 2nd bone id will always be the last weight, with the weight value dropped
								newVert.m_BoneWeightsPacked.weight[0] = (vertVvd->m_BoneWeights.weights.packedweight.weight[0]);
								newVert.m_BoneWeightsPacked.bone[0] = boneMap.find(vertVvd->m_BoneWeights.bone[0])->second;

								newVert.m_BoneWeightsPacked.numbones = (vertVvd->m_BoneWeights.numbones - 1);
							}
							else
							{
								// "simple" weights
								// use vvd bone count instead of previously set as it should never be funky
								for (int n = 0; n < vertVvd->m_BoneWeights.numbones; n++)
								{
									// don't set weight for third because it gets dropped
									// third weight is gotten by subtracting the weights that were not dropped from 1.0f
									if (n < 2)
									{
										newVert.m_BoneWeightsPacked.weight[n] = (vertVvd->m_BoneWeights.weights.weight[n] * 32767.0); // "pack" float into short, weight will always be <= 1.0f
									}
										
									newVert.m_BoneWeightsPacked.bone[n] = boneMap.find(vertVvd->m_BoneWeights.bone[n])->second;
								}

								newVert.m_BoneWeightsPacked.numbones = (vertVvd->m_BoneWeights.numbones - 1);
							}

							mstudioboneweight_t newLegacyWeight{};

							newLegacyWeight = vertVvd->m_BoneWeights;

							legacyWeights.push_back(newLegacyWeight);

							// check header flags so we don't pull color or uv2 when we don't want it
							if (rmdl->flags & STUDIOHDR_FLAGS_USES_UV2)
							{
								Vector2* uvlayer = vvc->uv(i);

								newVert.m_vecTexCoord2.x = uvlayer->x;
								newVert.m_vecTexCoord2.y = uvlayer->y;
							}

							if (rmdl->flags & STUDIOHDR_FLAGS_USES_VERTEX_COLOR)
							{
								VertexColor_t* color = vvc->color(i);

								newVert.m_color.r = color->r;
								newVert.m_color.g = color->g;
								newVert.m_color.b = color->b;
								newVert.m_color.a = color->a;
							}

							vertices.push_back(newVert);
						}

						int stripOffset = strips.size();
						strips.resize(strips.size() + stripGroup->numStrips);
						std::memcpy(strips.data() + stripOffset, stripGroup->strip(0), stripGroup->numStrips * sizeof(StripHeader_t));

						int indicesOffset = indices.size();
						indices.resize(indices.size() + stripGroup->numIndices);
						std::memcpy(indices.data() + indicesOffset, stripGroup->indices(), stripGroup->numIndices * sizeof(uint16_t));
					}

					vtxVertOffset += rmdlMesh->numvertices;

					newMesh.externalWeightSize = externalWeightIdx * sizeof(mstudioexternalweight_t);

					if (newMesh.numVerts == 0)
					{
						newMesh.vertCacheSize = 0;
						newMesh.flags = 0x0;
					}

					//printf("mesh verts %i \n", rmdlMesh->numvertices);

					meshes.push_back(newMesh);
				}
			}
		}
	}

	std::vector<ModelLODHeader_VG_t> newLods;

	ModelLODHeader_VG_t tempLod{};
	for (int i = 0; i < lods.size(); ++i)
	{
		if (lods[i].switchPoint != tempLod.switchPoint)
		{
			if(tempLod.meshCount > 0)
				newLods.push_back(tempLod);

			tempLod.meshIndex = tempLod.meshCount;
			tempLod.switchPoint = lods[i].switchPoint;
			tempLod.meshCount = 0;
		}
		if (lods[i].meshCount == 0)
			continue;

		tempLod.meshCount += lods[i].meshCount;
	}

	if (tempLod.meshCount > 0)
		newLods.push_back(tempLod);

	// cycle through lods and set correct mesh index
	for (int i = 0; i < newLods.size(); i++)
	{
		newLods.at(i).meshIndex = (meshes.size() / newLods.size()) * i;
	}

	lods = newLods;

	BinaryIO io;
	io.open(ChangeExtension(filePath, "vg"), BinaryIOMode::Write);

	VertexGroupHeader_t header{};

	header.numMeshes = meshes.size();
	header.numIndices = indices.size();
	header.numVerts = vertices.size() * vertCacheSize;
	header.numLODs = lods.size();
	header.numStrips = strips.size();
	header.externalWeightsSize = externalWeights.size() * sizeof(mstudioexternalweight_t);
	header.numLegacyWeights = legacyWeights.size();
	header.numUnknown = header.numMeshes / header.numLODs;
	header.numBoneStateChanges = BoneStates.size();

	io.write(header);

	header.boneStateChangeOffset = io.tell();
	io.getWriter()->write((char*)BoneStates.data(), BoneStates.size() * sizeof(uint8_t));

	header.meshOffset = io.tell();
	io.getWriter()->write((char*)meshes.data(), meshes.size() * sizeof(MeshHeader_VG_t));
	
	header.indexOffset = io.tell();
	io.getWriter()->write((char*)indices.data(), indices.size() * sizeof(uint16_t));
	
	header.vertOffset = io.tell();
	// write vertcies based on flags so we can have vvc stuff, and potentially other flag based stuff in the future
	for (int i = 0; i < vertices.size(); i++)
	{
		Vertex_VG_t vertex = vertices.at(i);

		if (meshes[0].flags & VG_PACKED_POSITION)
			io.getWriter()->write((char*)&vertex.m_vecPositionPacked, sizeof(Vector64));
		else
			io.getWriter()->write((char*)&vertex.m_vecPosition, sizeof(Vector3));

		io.getWriter()->write((char*)&vertex.m_BoneWeightsPacked, sizeof(mstudiopackedboneweight_t));
		io.getWriter()->write((char*)&vertex.m_NormalTangentPacked, sizeof(uint32_t));

		if (rmdl->flags & STUDIOHDR_FLAGS_USES_VERTEX_COLOR)
			io.getWriter()->write((char*)&vertex.m_color, sizeof(VertexColor_t));

		io.getWriter()->write((char*)&vertex.m_vecTexCoord, sizeof(Vector2));

		if (rmdl->flags & STUDIOHDR_FLAGS_USES_UV2)
			io.getWriter()->write((char*)&vertex.m_vecTexCoord2, sizeof(Vector2));
	}

	header.externalWeightOffset = io.tell();
	io.getWriter()->write((char*)externalWeights.data(), externalWeights.size() * sizeof(mstudioexternalweight_t));

	char* unknownBuf = new char[header.numUnknown * 0x30] {};
	header.unknownOffset = io.tell();
	io.getWriter()->write((char*)unknownBuf, header.numUnknown * 0x30);

	header.lodOffset = io.tell();
	io.getWriter()->write((char*)lods.data(), lods.size() * sizeof(ModelLODHeader_VG_t));

	header.legacyWeightOffset = io.tell();
	io.getWriter()->write((char*)legacyWeights.data(), legacyWeights.size() * sizeof(mstudioboneweight_t));

	header.stripOffset = io.tell();
	io.getWriter()->write((char*)strips.data(), strips.size() * sizeof(StripHeader_t));

	header.dataSize = io.tell();

	io.seek(0);
	io.write(header);

	io.close();
}
