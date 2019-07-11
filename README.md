This project shows a way to use ray tracing to simulate global illumination in real-time using DXR.

My approach is inspired by Reflective Shadow Maps, but with ray tracing to determine occlusion. So, instead of shooting a diffuse ray from the hit point, I sample a texture containing all points being hit by direct light, and shoot a ray to some of those points.

To reduce the noise I use a temporal filter (separately for direct and indirect light) and then a spatial filter. I also have some adaptive sampling in the sense that I shoot some extra rays where there just have been a disocclusion to compensate for the discarded colour history.

See results in Images folder