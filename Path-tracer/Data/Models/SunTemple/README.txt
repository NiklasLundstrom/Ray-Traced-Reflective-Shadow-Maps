Part of Unreal Engine 4 sample scenes pack, the Sun Temple was designed to showcase mobile features. This single level contains detailed PBR materials useful for a variety of graphics features and techniques.
Exported directly from the Unreal Engine by Kai-Hwa Yao and Nicholas Hull.

Model (SunTemple.fbx) contains the following vertex attributes:
- Positions
- Normals
- Tangents
- Bitangents
- 2 sets of texture coordinates:
	Set 0 (Materials)
	Set 1 (Lightmap UV)
- 1,641,711 vertices

Textures (compressed .DDS) designed for GGX-based, metal-rough PBR material system with the following convention: 
- BaseColor
	RGB channels: BaseColor value
	Alpha channel: Opacity

- Specular:
	Red channel: Occlusion
	Green channel: Roughness
	Blue channel: Metalness

- Normal (DirectX)

Also included:
- Spherical reflection HDR map from UE4 scene
- Spherical environment HDR map from UE4 scene
- Falcor scene file (SunTemple.fscene) to be used with Falcor's forward renderer demo. Contains lighting and camera information.

How to cite use of this asset:

  @misc{OrcaUE4SunTemple,
   title = {Unreal Engine Sun Temple, Open Research Content Archive (ORCA)},
   author = {Epic Games},
   year = {2017},
   month = {October},
   note = {\small \texttt{http://developer.nvidia.com/orca/epic-games-sun-temple}},
   url = {http://developer.nvidia.com/orca/epic-games-sun-temple}
}