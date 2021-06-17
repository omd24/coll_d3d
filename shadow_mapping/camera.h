#pragma once

#include <directxmath.h>

struct Camera;
size_t
Camera_CalculateRequiredSize ();
void
Camera_Init (Camera * cam);
void
Camera_SetLens (Camera * cam, float fov_y, float aspect, float zn, float zf);
DirectX::XMVECTOR
Camera_GetPosition (Camera * cam);
DirectX::XMFLOAT3
Camera_GetPosition3f (Camera * cam);
void
Camera_SetPosition (Camera * cam, float x, float y, float z);
void
Camera_SetPosition (Camera * cam, DirectX::XMFLOAT3 * v);
DirectX::XMVECTOR
Camera_GetRight (Camera * cam);
DirectX::XMFLOAT3
Camera_GetRight3f (Camera * cam);
DirectX::XMVECTOR
Camera_GetUp (Camera * cam);
DirectX::XMFLOAT3
Camera_GetUp3f (Camera * cam);
DirectX::XMVECTOR
Camera_GetLook (Camera * cam);
DirectX::XMFLOAT3
Camera_GetLook3f (Camera * cam);
float
Camera_GetNearZ (Camera * cam);
float
Camera_GetFarZ (Camera * cam);
float
Camera_GetAspect (Camera * cam);
float
Camera_GetFovY (Camera * cam);
float
Camera_GetFovX (Camera * cam);
float
Camera_GetNearWindowWidth (Camera * cam);
float
Camera_GetNearWindowHeight (Camera * cam);
float
Camera_GetFarWindowWidth (Camera * cam);
float
Camera_GetFarWindowHeight (Camera * cam);
void
Camera_LookAt (Camera * cam, DirectX::FXMVECTOR * pos, DirectX::FXMVECTOR * target, DirectX::FXMVECTOR * world_up);
void
Camera_LookAt (Camera * cam, DirectX::XMFLOAT3 * pos, DirectX::XMFLOAT3 * target, DirectX::XMFLOAT3 * up);
DirectX::XMMATRIX
Camera_GetView (Camera * cam);
DirectX::XMMATRIX
Camera_GetProj (Camera * cam);
DirectX::XMFLOAT4X4
Camera_GetView4x4f (Camera * cam);
DirectX::XMFLOAT4X4
Camera_GetProj4x4f (Camera * cam);
void
Camera_Strafe (Camera * cam, float d);
void
Camera_Walk (Camera * cam, float d);
void
Camera_Pitch (Camera * cam, float angle);
void
Camera_RotateY (Camera * cam, float angle);
void
Camera_UpdateViewMatrix (Camera * cam);
