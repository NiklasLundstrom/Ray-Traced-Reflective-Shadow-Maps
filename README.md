This project shows a way to use ray tracing to simulate global illumination in real-time using DXR.

My approach is inspired by Reflective Shadow Maps, but with ray tracing to determine occlusion. So, instead of shooting a diffuse ray from the hit point, I sample a texture containing all points being hit by direct light, and shoot a shadow ray to some of these points. This simplifies the ray tracing pass in the sense that it first of all only needs shadow rays to create colour bleeding, and also that it moves the unique shader resources for each mesh (textures etc.) to the rasterization instead.

To reduce the noise I use a temporal filter (separately for direct and indirect light) and then a bilateral spatial filter. I also have some adaptive sampling in the sense that I shoot some extra rays where there just have been a disocclusion to compensate for the discarded colour history.

The result is more or less noise free images with high quality lighting in real-time.

See some results in Images folder and in the youtube videos: [Part 1](https://youtu.be/Dbwxm-EEsRI) and [Part 2 (final results)](https://youtu.be/EPt_8N6pm3o)

![test](https://bitbucket.org/NiklasLundstroem/real-time-global-illumination-using-ray-tracing/raw/b75c34910c1a3d532122a1b9051d6f1f19287feb/Images/sunTemple.png)