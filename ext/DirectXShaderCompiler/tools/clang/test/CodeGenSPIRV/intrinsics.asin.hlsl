// RUN: %dxc -T vs_6_0 -E main

// According to HLSL reference:
// The 'asin' function can only operate on float, vector of float, and matrix of float.

// CHECK:      [[glsl:%\d+]] = OpExtInstImport "GLSL.std.450"

void main() {
  float result;
  float2 result2;
  float3 result3;
  float4 result4;
  float3x2 result3x2;

// CHECK:      [[a:%\d+]] = OpLoad %float %a
// CHECK-NEXT: [[asin_a:%\d+]] = OpExtInst %float [[glsl]] Asin [[a]]
// CHECK-NEXT: OpStore %result [[asin_a]]
  float a;
  result = asin(a);

// CHECK-NEXT: [[b:%\d+]] = OpLoad %float %b
// CHECK-NEXT: [[asin_b:%\d+]] = OpExtInst %float [[glsl]] Asin [[b]]
// CHECK-NEXT: OpStore %result [[asin_b]]
  float1 b;
  result = asin(b);

// CHECK-NEXT: [[c:%\d+]] = OpLoad %v3float %c
// CHECK-NEXT: [[asin_c:%\d+]] = OpExtInst %v3float [[glsl]] Asin [[c]]
// CHECK-NEXT: OpStore %result3 [[asin_c]]
  float3 c;
  result3 = asin(c);

// CHECK-NEXT: [[d:%\d+]] = OpLoad %float %d
// CHECK-NEXT: [[asin_d:%\d+]] = OpExtInst %float [[glsl]] Asin [[d]]
// CHECK-NEXT: OpStore %result [[asin_d]]
  float1x1 d;
  result = asin(d);

// CHECK-NEXT: [[e:%\d+]] = OpLoad %v2float %e
// CHECK-NEXT: [[asin_e:%\d+]] = OpExtInst %v2float [[glsl]] Asin [[e]]
// CHECK-NEXT: OpStore %result2 [[asin_e]]
  float1x2 e;
  result2 = asin(e);

// CHECK-NEXT: [[f:%\d+]] = OpLoad %v4float %f
// CHECK-NEXT: [[asin_f:%\d+]] = OpExtInst %v4float [[glsl]] Asin [[f]]
// CHECK-NEXT: OpStore %result4 [[asin_f]]
  float4x1 f;
  result4 = asin(f);

// CHECK-NEXT: [[g:%\d+]] = OpLoad %mat3v2float %g
// CHECK-NEXT: [[g_row0:%\d+]] = OpCompositeExtract %v2float [[g]] 0
// CHECK-NEXT: [[asin_g_row0:%\d+]] = OpExtInst %v2float [[glsl]] Asin [[g_row0]]
// CHECK-NEXT: [[g_row1:%\d+]] = OpCompositeExtract %v2float [[g]] 1
// CHECK-NEXT: [[asin_g_row1:%\d+]] = OpExtInst %v2float [[glsl]] Asin [[g_row1]]
// CHECK-NEXT: [[g_row2:%\d+]] = OpCompositeExtract %v2float [[g]] 2
// CHECK-NEXT: [[asin_g_row2:%\d+]] = OpExtInst %v2float [[glsl]] Asin [[g_row2]]
// CHECK-NEXT: [[asin_matrix:%\d+]] = OpCompositeConstruct %mat3v2float [[asin_g_row0]] [[asin_g_row1]] [[asin_g_row2]]
// CHECK-NEXT: OpStore %result3x2 [[asin_matrix]]
  float3x2 g;
  result3x2 = asin(g);
}
