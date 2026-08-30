# LinuxProcessMemoryRadar
A Linux tool that reads live entity positions with process_vm_readv to drive a real-time raylib radar. Three iterations: manual assembly reading with gdb + scanmem, then PINCE-based pointer-chain hunting, then source-verified offsets confirmed against the game's own C++ structs.
