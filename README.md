<img src="band.svg" width="100%"/>

# libCerfEngine

An experimental homebrew cross-platform JIT compiler.

Niceties:
- designed to run Windows CE, which is an operating system, therefore is optimized for low level execution unlike some other very popular JITs
- oriented by architecture for several guest archs
- runs today ARMv4-ARMv7-A, Thumb/Thumb-2, VFP and NEON. Has TLB with FCSE and ASID support
- also runs MIPS I, III, IV and MIPS16
- not every subsystem above is implemented to the end - find halts in code if you need a list. However it's enough for our WinCE emulator as of today.
- Claude has verified this code dozens times against arch manuals and added citations everywhere but also found hundreds of errors which got fixed eventually against the same arch docs. At least this makes me believe that claude-verified parts are very correct. Probably.

Built and used for [CE Runtime Foundation](https://github.com/gweslab/cerf) emulator. Earlier this JIT was inside the emulator and heavily coupled with it. It might still be coupled in some places since nobody put much effort into decoupling procedure.  ``¯\_(ツ)_/¯``

> [!NOTE]
> Hey - if you are going to use this JIT in your emulator I would appreciate and shake your hand if you would mention that your app runs CE Runtime Foundation Engine or something like this. Thanks.

I'm having no plans on updating this README so expect it's already rotten and misinforms you. Visit [CE Runtime Foundation repo](https://github.com/gweslab/cerf) for the first class usage example.

## CerfIR and the compile chain

Our brand new Intermediate Representation Layer™ conception we ~~stole~~ borrowed from [friends](THIRD_PARTY_NOTICES.md).

Since you are reading README for JIT in 2026 you probably already know something about recompilers and how those fancy IR layers are built and why. 

But if you don't I assume you therefore want to gain some knowledge. I'll try my best:
1. **Decoder** "what is this instruction?"
   - the guest program is basically a number, the decored reads each at the time
   - for each numbers it answers which instruction is this and what registers are used for it
   - it reads until jump/branch - at this point this becomes a "block" - a short piece of code that the engine translates together
2. **Translator** "what does this instruction mean?"
   - each instructions has its own translator
   - it takes an instruction from the decoder and describes in high level what this instruction does
   - e.g. "read this register, add this number, write this register"
   - it already becomes a more shared representation rather than original view
   - it knows everything about guest arch and nothing about host
3. **CerfIR Builder** "record logic from translator and make it smaller"
   - translator says the logic it knows to the builder
   - it makes this logic simpler if it is possible
   - at this point this logic becomes shared - several archs instructions will eventually lead to single IR representation
   - that's exactly how we reach cross platform at lower price - we translate this simple layer to host, not the original instructions directly
   - it records results and forgets what's not needed - optimizes
4. **Lowering** "make it in host language"
   - simple steps from IR are turned into host instructions
   - knows everything about host archs, picks appropriate registers for stuff
   - writes host instructions into memory where they then get executed at full speed
   - also handles more complex situations, e.g. defines what to do when a memory access go to a device instead of RAM and many more other scenarios

## Embedding libCerfEngine into your project

### 1. Submodule clone and CMake

```
git submodule add https://github.com/gweslab/libCerfEngine.git external/libCerfEngine
git submodule update --init --recursive
```

```cmake
add_subdirectory(external/libCerfEngine EXCLUDE_FROM_ALL)
target_link_libraries(my_emulator PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,cerfengine::cerfengine>")
```


### 2. Start the engine

```cpp
cerfengine::Setup setup;
setup.isa = cerfengine::Isa::kArm;
// point the setup at your host, memory, MMIO bus and ARM system host

cerfengine::Engine engine(setup);
std::string        error;
if (!engine.Start(error)) {
    std::fprintf(stderr, "engine refused the setup: %s\n", error.c_str());
    return 1;
}
engine.BindClock(my_clock);

cerfengine::ArmCpu& cpu = engine.Get<cerfengine::ArmCpu>();
cpu.SetInitialStackPointer(stack_top);
cpu.RaiseResetException(entry_pa, false);

cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
while (guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning) {
    // your devices, interrupts and UI get their turn here
}
std::fprintf(stderr, "guest halted: %s\n", engine.LastHalt().detail);
```

### 3. Failed runs

- `Start`  might refuse a setup that is impossible. See the error in the the string.
- `Run` returns halted when the guest does something the engine does not model -- `LastHalt()` holds the reason.
- Your host's `Panic` when something went wrong in an engine and.. it's a bug probably

## Tests

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

## License

[MIT](LICENSE) /  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
