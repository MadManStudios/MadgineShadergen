// FXC command line: fxc /T vs_5_0 %s /Fo %t.dxbc
// RUN: %dxbc2dxil %t.dxbc /emit-llvm /o %t.ll.converted
// RUN: fc %b.ref %t.ll.converted




float4 main(float4 a : A) : SV_POSITION
{
  return a;
}
