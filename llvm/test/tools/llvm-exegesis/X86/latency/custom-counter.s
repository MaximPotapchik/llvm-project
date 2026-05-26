# REQUIRES: x86-registered-target

# RUN: llvm-exegesis -mtriple=x86_64-unknown-unknown -mcpu=x86-64 --mode=latency --opcode-name=ADD64rr --use-dummy-perf-counters --custom-counter=r003c 2>&1 | FileCheck %s
# RUN: llvm-exegesis -mtriple=x86_64-unknown-unknown -mcpu=x86-64 --mode=latency --opcode-name=ADD64rr --use-dummy-perf-counters --custom-counter=event=0x3c,umask=0x00 2>&1 | FileCheck %s
# RUN: not llvm-exegesis -mtriple=x86_64-unknown-unknown -mcpu=x86-64 --mode=latency --opcode-name=ADD64rr --use-dummy-perf-counters --custom-counter=rZZZZ 2>&1 | FileCheck %s --check-prefix=INVALID
# RUN: not llvm-exegesis -mtriple=x86_64-unknown-unknown -mcpu=x86-64 --mode=latency --opcode-name=ADD64rr --use-dummy-perf-counters --custom-counter=event=0xZZ 2>&1 | FileCheck %s --check-prefix=BADEVENT

# CHECK: mode: latency
# CHECK: ADD64rr
# CHECK: error: ''

# INVALID: invalid raw counter spec

# BADEVENT: invalid event value
