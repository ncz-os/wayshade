# Fractasia gap ledger

The [source catalogue](COVERAGE-MATRIX.md) lists all 173 registered Xfractint
20.04p16 type slots. Every entry currently has **unverified** mathematical
parity. The 13 NCZ shader labels are implementation labels, not 13 complete
Fractint families. The former 67/184 percentage and claims about nonexistent
Mandelbulb/Mandelbox entries have been withdrawn.

## Work needed for full coverage

1. Establish reference equations, parameters, bailout rules and representative
   frames for each of the 107 visible built-in types. Record an explicit
   equivalence or separate implementation for each of the 66 hidden numeric
   and internal variants. Many share a rendering dispatch callback but use
   different orbit/setup functions.
2. Implement and test algorithm families the current fragment-only renderer
   does not cover. The source includes plasma, affine IFS, L-systems,
   bifurcation, Lyapunov, cellular automata, diffusion, inverse Julia, ant,
   orbit plots, Newton/Halley basins and several 4D paths. Similar colour or
   silhouette is insufficient.
3. Evaluate multipass GPU designs for stateful algorithms: ping-pong textures
   for cellular systems, accumulation or compute for particle processes, and
   intermediate textures for compositing. Measure target iGPU and discrete GPU
   frame time and memory before choosing quality tiers. The current one-pass
   design does not justify declaring these types impossible or permanently
   out of scope.
4. Define the scope of user-supplied formula, IFS and L-system definitions.
   These make the total set of possible outputs open-ended; a finite
   percentage can only describe a stated built-in catalogue.
5. Capture matched reference and NCZ frames at multiple seeds, parameters,
   zooms and times. Check numeric stability, visual fidelity and sustained
   performance on shipping GPUs. Record provenance and a per-type verdict.

No row in the catalogue is yet marked complete by those acceptance rules.
That is a verification state, not a claim that the current shaders render
nothing.
