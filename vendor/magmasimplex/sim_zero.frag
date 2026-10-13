// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_zero.frag — clear a field to zero (and a=1). Used to zero the
// pressure texture before each projection iteration set.
//
// u_src is ignored.

precision highp float;
out vec4 fragColor;

void main(){
  fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}