#include "camera.h"
#include "headers/common.h"

using namespace DirectX;

struct Camera {
    // camera coordinates relative to world
    XMFLOAT3 position;
    XMFLOAT3 right;
    XMFLOAT3 up;
    XMFLOAT3 look;

    // frustum props
    float near_z;
    float far_z;
    float aspect;
    float fov_y;
    float near_wnd_height;
    float far_wnd_height;

    bool view_dirty;

    XMFLOAT4X4 view;
    XMFLOAT4X4 proj;
};
size_t
Camera_CalculateRequiredSize () {
    return sizeof(Camera);
}
void
Camera_Init (Camera * cam) {
    _ASSERT_EXPR(cam, _T("Invalid Camera Pointer"));
    memset(cam, 0, sizeof(Camera));
    cam->position = {0.0f, 0.0f, 0.0f};
    cam->right = {1.0f, 0.0f, 0.0f};
    cam->up = {0.0f, 1.0f, 0.0f};
    cam->look = {0.0f, 0.0f, 1.0f};
    cam->view_dirty = true;

    Camera_SetLens(cam, 0.25f * XM_PI, 1.0f, 1.0f, 1000.0f);
}
void
Camera_SetLens (Camera * cam, float fov_y, float aspect, float zn, float zf) {
    cam->fov_y = fov_y;
    cam->aspect = aspect;
    cam->near_z = zn;
    cam->far_z = zf;

    cam->near_wnd_height = 2.0f * cam->near_z * tanf(0.5f * fov_y);
    cam->far_wnd_height = 2.0f * cam->far_z * tanf(0.5f * fov_y);

    XMMATRIX p = XMMatrixPerspectiveFovLH(
        cam->fov_y,
        cam->aspect,
        cam->near_z,
        cam->far_z
    );
    XMStoreFloat4x4(&cam->proj, p);
}
XMVECTOR
Camera_GetPosition (Camera * cam) {
    return XMLoadFloat3(&cam->position);
}
XMFLOAT3
Camera_GetPosition3f (Camera * cam) {
    return cam->position;
}
void
Camera_SetPosition (Camera * cam, float x, float y, float z) {
    cam->position = XMFLOAT3(x, y, z);
    cam->view_dirty = true;
}
void
Camera_SetPosition (Camera * cam, XMFLOAT3 * v) {
    cam->position = *v;
    cam->view_dirty = true;
}
XMVECTOR
Camera_GetRight (Camera * cam) {
    return XMLoadFloat3(&cam->right);
}
XMFLOAT3
Camera_GetRight3f (Camera * cam) {
    return cam->right;
}
XMVECTOR
Camera_GetUp (Camera * cam) {
    return XMLoadFloat3(&cam->up);
}
XMFLOAT3
Camera_GetUp3f (Camera * cam) {
    return cam->up;
}
XMVECTOR
Camera_GetLook (Camera * cam) {
    return XMLoadFloat3(&cam->look);
}
XMFLOAT3
Camera_GetLook3f (Camera * cam) {
    return cam->look;
}
float
Camera_GetNearZ (Camera * cam) {
    return cam->near_z;
}
float
Camera_GetFarZ (Camera * cam) {
    return cam->far_z;
}
float
Camera_GetAspect (Camera * cam) {
    return cam->aspect;
}
float
Camera_GetFovY (Camera * cam) {
    return cam->fov_y;
}
float
Camera_GetFovX (Camera * cam) {
    float half_width = 0.5f * Camera_GetNearWindowWidth(cam);
    return 2.0f * atanf(half_width / cam->near_z);
}
float
Camera_GetNearWindowWidth (Camera * cam) {
    return cam->aspect * cam->near_wnd_height;
}
float
Camera_GetNearWindowHeight (Camera * cam) {
    return cam->near_wnd_height;
}
float
Camera_GetFarWindowWidth (Camera * cam) {
    return cam->aspect * cam->far_wnd_height;
}
float
Camera_GetFarWindowHeight (Camera * cam) {
    return cam->far_wnd_height;
}
void
Camera_LookAt (Camera * cam, FXMVECTOR * pos, FXMVECTOR * target, FXMVECTOR * world_up) {
    XMVECTOR L = XMVector3Normalize(XMVectorSubtract(*target, *pos));
    XMVECTOR R = XMVector3Normalize(XMVector3Cross(*world_up, L));
    XMVECTOR U = XMVector3Cross(L, R);

    XMStoreFloat3(&cam->position, *pos);
    XMStoreFloat3(&cam->look, L);
    XMStoreFloat3(&cam->right, R);
    XMStoreFloat3(&cam->up, U);

    cam->view_dirty = true;
}
void
Camera_LookAt (Camera * cam, XMFLOAT3 * pos, XMFLOAT3 * target, XMFLOAT3 * up) {
    XMVECTOR P = XMLoadFloat3(pos);
    XMVECTOR T = XMLoadFloat3(target);
    XMVECTOR U = XMLoadFloat3(up);

    Camera_LookAt(cam, &P, &T, &U);

    cam->view_dirty = true;
}
XMMATRIX
Camera_GetView (Camera * cam) {
    _ASSERT_EXPR(false == cam->view_dirty, _T("Invalid Camera"));
    return XMLoadFloat4x4(&cam->view);
}
XMMATRIX
Camera_GetProj (Camera * cam) {
    return XMLoadFloat4x4(&cam->proj);
}
XMFLOAT4X4
Camera_GetView4x4f (Camera * cam) {
    _ASSERT_EXPR(false == cam->view_dirty, _T("Invalid Camera"));
    return cam->view;
}
XMFLOAT4X4
Camera_GetProj4x4f (Camera * cam) {
    return cam->proj;
}
void
Camera_Strafe (Camera * cam, float d) {
    // cam->position += d*cam->right
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR r = XMLoadFloat3(&cam->right);
    XMVECTOR p = XMLoadFloat3(&cam->position);
    XMStoreFloat3(&cam->position, XMVectorMultiplyAdd(s, r, p));

    cam->view_dirty = true;
}
void
Camera_Walk (Camera * cam, float d) {
    // cam->position += d*cam->look
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR l = XMLoadFloat3(&cam->look);
    XMVECTOR p = XMLoadFloat3(&cam->position);
    XMStoreFloat3(&cam->position, XMVectorMultiplyAdd(s, l, p));

    cam->view_dirty = true;
}
void
Camera_Pitch (Camera * cam, float angle) {
    // Rotate up and look vector about the right vector.

    XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&cam->right), angle);

    XMStoreFloat3(&cam->up, XMVector3TransformNormal(XMLoadFloat3(&cam->up), R));
    XMStoreFloat3(&cam->look, XMVector3TransformNormal(XMLoadFloat3(&cam->look), R));

    cam->view_dirty = true;
}
void
Camera_RotateY (Camera * cam, float angle) {
    // Rotate the basis vectors about the world y-axis.

    XMMATRIX R = XMMatrixRotationY(angle);

    XMStoreFloat3(&cam->right, XMVector3TransformNormal(XMLoadFloat3(&cam->right), R));
    XMStoreFloat3(&cam->up, XMVector3TransformNormal(XMLoadFloat3(&cam->up), R));
    XMStoreFloat3(&cam->look, XMVector3TransformNormal(XMLoadFloat3(&cam->look), R));

    cam->view_dirty = true;
}
void
Camera_UpdateViewMatrix (Camera * cam) {
    if (cam->view_dirty) {
        XMVECTOR R = XMLoadFloat3(&cam->right);
        XMVECTOR U = XMLoadFloat3(&cam->up);
        XMVECTOR L = XMLoadFloat3(&cam->look);
        XMVECTOR P = XMLoadFloat3(&cam->position);

        // Keep camera's axes orthogonal to each other and of unit length.
        L = XMVector3Normalize(L);
        U = XMVector3Normalize(XMVector3Cross(L, R));

        // U, L already ortho-normal, so no need to normalize cross product.
        R = XMVector3Cross(U, L);

        // Fill in the view matrix entries.
        float x = -XMVectorGetX(XMVector3Dot(P, R));
        float y = -XMVectorGetX(XMVector3Dot(P, U));
        float z = -XMVectorGetX(XMVector3Dot(P, L));

        XMStoreFloat3(&cam->right, R);
        XMStoreFloat3(&cam->up, U);
        XMStoreFloat3(&cam->look, L);

        cam->view(0, 0) = cam->right.x;
        cam->view(1, 0) = cam->right.y;
        cam->view(2, 0) = cam->right.z;
        cam->view(3, 0) = x;

        cam->view(0, 1) = cam->up.x;
        cam->view(1, 1) = cam->up.y;
        cam->view(2, 1) = cam->up.z;
        cam->view(3, 1) = y;

        cam->view(0, 2) = cam->look.x;
        cam->view(1, 2) = cam->look.y;
        cam->view(2, 2) = cam->look.z;
        cam->view(3, 2) = z;

        cam->view(0, 3) = 0.0f;
        cam->view(1, 3) = 0.0f;
        cam->view(2, 3) = 0.0f;
        cam->view(3, 3) = 1.0f;

        cam->view_dirty = false;
    }
}

