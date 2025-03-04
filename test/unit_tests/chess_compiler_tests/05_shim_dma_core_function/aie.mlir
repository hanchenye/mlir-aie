//===- aie.mlir ------------------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// REQUIRES: valid_xchess_license
// RUN: xchesscc -p me -P ${CARDANO}/data/cervino/lib -c %S/kernel.cc
// RUN: aiecc.py --xbridge --sysroot=%VITIS_SYSROOT% %s -I%S/../../../../runtime_lib/ %S/../../../../runtime_lib/test_library.cpp %S/test.cpp -o test.elf
// RUN: %run_on_board ./test.elf

module @test_chess_04_deprecated_shim_dma_precompiled_kernel{
  %t73 = AIE.tile(7, 3)
  %t72 = AIE.tile(7, 2)
  %t71 = AIE.tile(7, 1)
  %t70 = AIE.tile(7, 0)

  %buf_a_ping = AIE.buffer(%t73) {sym_name = "a_ping" } : memref<256xi32>
  %buf_a_pong = AIE.buffer(%t73) {sym_name = "a_pong" } : memref<256xi32>
  %buf_b_ping = AIE.buffer(%t73) {sym_name = "b_ping" } : memref<256xi32>
  %buf_b_pong = AIE.buffer(%t73) {sym_name = "b_pong" } : memref<256xi32>

  %lock_a_ping = AIE.lock(%t73, 3) // a_ping
  %lock_a_pong = AIE.lock(%t73, 4) // a_pong
  %lock_b_ping = AIE.lock(%t73, 5) // b_ping
  %lock_b_pong = AIE.lock(%t73, 6) // b_pong

  func private @func(%A: memref<256xi32>, %B: memref<256xi32>) -> ()

  %c13 = AIE.core(%t73) { 
    
    %lb = arith.constant 0 : index
    %ub = arith.constant 1 : index
    %step = arith.constant 1 : index
    
    scf.for %iv = %lb to %ub step %step {
      
      AIE.useLock(%lock_a_ping, "Acquire", 1) // acquire for read
      AIE.useLock(%lock_b_ping, "Acquire", 0) // acquire for write
      call @func(%buf_a_ping, %buf_b_ping) : (memref<256xi32>, memref<256xi32>) -> ()
      AIE.useLock(%lock_a_ping, "Release", 0) // release for write
      AIE.useLock(%lock_b_ping, "Release", 1) // release for read

      AIE.useLock(%lock_a_pong, "Acquire", 1) // acquire for read
      AIE.useLock(%lock_b_pong, "Acquire", 0) // acquire for write
      call @func(%buf_a_pong, %buf_b_pong) : (memref<256xi32>, memref<256xi32>) -> ()
      AIE.useLock(%lock_a_pong, "Release", 0) // release for write
      AIE.useLock(%lock_b_pong, "Release", 1) // release for read
      
    }

    AIE.end
  } { link_with="kernel.o" }

  // Tile DMA
  %m73 = AIE.mem(%t73) {
      %srcDma = AIE.dmaStart("S2MM0", ^bd0, ^dma0)
    ^dma0:
      %dstDma = AIE.dmaStart("MM2S1", ^bd2, ^end)
    ^bd0:
      AIE.useLock(%lock_a_ping, "Acquire", 0)
      AIE.dmaBd(<%buf_a_ping : memref<256xi32>, 0, 256>, 0)
      AIE.useLock(%lock_a_ping, "Release", 1)
      cf.br ^bd1
    ^bd1:
      AIE.useLock(%lock_a_pong, "Acquire", 0)
      AIE.dmaBd(<%buf_a_pong : memref<256xi32>, 0, 256>, 0)
      AIE.useLock(%lock_a_pong, "Release", 1)
      cf.br ^bd0
    ^bd2:
      AIE.useLock(%lock_b_ping, "Acquire", 1)
      AIE.dmaBd(<%buf_b_ping : memref<256xi32>, 0, 256>, 0)
      AIE.useLock(%lock_b_ping, "Release", 0)
      cf.br ^bd3
    ^bd3:
      AIE.useLock(%lock_b_pong, "Acquire", 1)
      AIE.dmaBd(<%buf_b_pong : memref<256xi32>, 0, 256>, 0)
      AIE.useLock(%lock_b_pong, "Release", 0)
      cf.br ^bd2
    ^end:
      AIE.end
  }

  // DDR buffer
  %buffer_in  = AIE.external_buffer 0x020100004000 : memref<512 x i32>
  %buffer_out = AIE.external_buffer 0x020100006000 : memref<512 x i32>

  // Shim DMA connection to kernel
  AIE.flow(%t71, "South" : 3, %t73, "DMA" : 0)
  AIE.flow(%t73, "DMA" : 1, %t71, "South" : 2)
  %sw1  = AIE.switchbox(%t70) {
    AIE.connect<"South" : 3, "North" : 3>
    AIE.connect<"North" : 2, "South" : 2>
  }
  %mux1 = AIE.shimmux  (%t70) {
    AIE.connect<"DMA"   : 0, "North" : 3> 
    AIE.connect<"North" : 2, "DMA" : 0>
  }

  // Shim DMA loads large buffer to local memory
  %dma = AIE.shimDMA(%t70) {
      %lock1 = AIE.lock(%t70, 1)
//      AIE.dmaStart(MM2S0, ^bd0, ^end)
      %lock2 = AIE.lock(%t70, 2)
      AIE.dmaStart(MM2S0, ^bd0, ^dma)
    ^dma:
      AIE.dmaStart(S2MM0, ^bd1, ^end)
    ^bd0:
      AIE.useLock(%lock1, Acquire, 1)
      AIE.dmaBd(<%buffer_in : memref<512 x i32>, 0, 512>, 0)
      AIE.useLock(%lock1, Release, 0)
      cf.br ^bd0
    ^bd1:
      AIE.useLock(%lock2, Acquire, 1)
      AIE.dmaBd(<%buffer_out : memref<512 x i32>, 0, 512>, 0)
      AIE.useLock(%lock2, Release, 0)
      cf.br ^bd1
    ^end:
      AIE.end
  }


}
