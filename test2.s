.function int32 ret3
  ldc.i4.3
  ret
.function int32 ret5
  ldc.i4.5
  ret
.function int32 add x:int32 y:int32
  ldarg.0
  ldarg.1
  add
  ret
.function int32 sub x:int32 y:int32
  ldarg.0
  ldarg.1
  sub
  ret
.function int32 add6 a:int32 b:int32 c:int32 d:int32 e:int32 f:int32
  ldarg.0
  ldarg.1
  ldarg.2
  ldarg.3
  ldarg.s 4
  ldarg.s 5
  add
  add
  add
  add
  add
  ret
