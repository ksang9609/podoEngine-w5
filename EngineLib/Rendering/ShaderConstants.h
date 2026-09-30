#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Color.h"

struct FConstants
{
	FMatrix World; //Model
	FMatrix ViewProjection;
	FLinearColor Tint;          // rgb = 색, a = 섞는 비율
};

struct FFrameConstants
{
	FMatrix ViewProjection;
};

struct alignas(256) FPerObjectConstants
{
	FMatrix World;
	FLinearColor Tint;
	FVector2 UVScale;
	FVector2 UVOffset;
	float Pad[40];
};
static_assert(sizeof(FPerObjectConstants) == 256, "FPerObjectConstants must be 256 bytes");

struct FTextureConstants
{
	FMatrix World; //Model
	FMatrix ViewProjection;
	FLinearColor Tint;          // rgb = 색, a = 섞는 비율
	FVector2 UVScale;       // 텍스처 좌표 스케일
	FVector2 UVOffset;      // 텍스처 좌표 오프셋
};

struct FBillboardConstants
{
	FVector3 Location;
	float Pad0 = 0;
	FVector3 Scale;
	float Pad1 = 0;

	FMatrix ViewProjection;
	FLinearColor Tint;

	FVector2 UVScale;
	FVector2 UVOffset;

	FVector3 CameraRight;
	float Pad2 = 0;
	FVector3 CameraUp;
	float Pad3 = 0;
};

struct alignas(16) FParticleConstants
{
	FVector3 Location;
	float pad0 = 0;
	FVector3 Scale;
	float pad1 = 0;

	FMatrix ViewProjection;

	FVector3 CameraRight;
	float pad2 = 0;
	FVector3 CameraUp;
	float pad3 = 0;

	FLinearColor Tint;

	int32 NumRows;
	int32 NumCols;
	int32 CurrentFrame;
	int32 NextFrame;

	float FrameRatio;
	float pad[3] = {};
};

// intancing 용
struct FInstanceData
{
	FMatrix World;
	FLinearColor Tint;
};

// HLSL의 b1에 전달할 데이터
struct FUnicodeFontConstants
{
	float DistanceRange = 4.0f;
	float Padding[3] = {};
};

struct FFontConstants
{
	FVector3 Location;
	float Pad0 = 0;
	FVector3 Scale;
	float Pad1 = 0;

	FVector3 CameraRight;
	float Pad2 = 0;
	FVector3 CameraUp;
	float Pad3 = 0;

	FMatrix ViewProjection;
	FLinearColor Tint;
};

/* Hightlight */
struct alignas(16) FMaskConstants
{
	FMatrix World;
	FMatrix ViewProjection;
};

struct alignas(16) FOutlineConstants
{
	FLinearColor OutlineColor;

	int ViewMin[2];
	int ViewMax[2];

	int RadiusPixels;
};
