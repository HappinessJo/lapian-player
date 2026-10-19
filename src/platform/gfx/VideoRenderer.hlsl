// T1.3 NV12/P010 -> BT.709 limited range RGB。
// 顶点由 CPU 生成（含 fit / display matrix 旋转），这里只做 YUV→RGB。
// 编译：
//   fxc /T vs_5_0 /E VSMain /Fo VideoVS.cso VideoRenderer.hlsl
//   fxc /T ps_5_0 /E PSMain /Fo VideoPS.cso VideoRenderer.hlsl

cbuffer Params : register(b0) {
  float g_bit_depth;  // 0 = 8-bit NV12, 1 = 10-bit P010
  float3 g_padding;
};

Texture2D<float> g_y : register(t0);
Texture2D<float2> g_uv : register(t1);
SamplerState g_sampler : register(s0);

struct VSInput {
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
  VSOutput output;
  output.position = float4(input.position, 0.0, 1.0);
  output.uv = input.uv;
  return output;
}

float4 PSMain(VSOutput input) : SV_Target {
  float y = g_y.Sample(g_sampler, input.uv).r;
  float2 uv = g_uv.Sample(g_sampler, input.uv).rg;

  if (g_bit_depth > 0.5) {
    // P010: 10-bit 码值在 16-bit 字的高 10 位，除以 64 还原。
    y *= 65535.0 / 64.0;
    uv *= 65535.0 / 64.0;
    y = (y - 64.0) / 876.0;
    uv = (uv - 512.0) / 896.0;
  } else {
    y *= 255.0;
    uv *= 255.0;
    y = (y - 16.0) / 219.0;
    uv = (uv - 128.0) / 224.0;
  }

  // BT.709 limited range
  float3 rgb;
  rgb.r = y + 1.5748 * uv.y;
  rgb.g = y - 0.1873 * uv.x - 0.4681 * uv.y;
  rgb.b = y + 1.8556 * uv.x;
  return float4(saturate(rgb), 1.0);
}
