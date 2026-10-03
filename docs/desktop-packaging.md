# Desktop runtime packages

`tooling::packageProject(project, destination, runtime, shaders, licenseRoot)`
keeps the original first three arguments and adds optional shader and license
roots. Existing headless packages still work when no shader directory is found.
The CLI forwards `--runtime`, `--shaders` and `--license-root`; its integration
must supply the actual engine source root when producing redistributable builds.
A caller that omits the license root gets an explicit omission marker rather
than silently treating authored project licenses as engine notices.

When `shaders` is omitted, the helper checks the runtime executable's build
parent (`bin/../shaders`). If present, that directory must contain
`mesh_textured.vert.spv` and `mesh_textured.frag.spv`, which the authored desktop
runtime needs. An explicit shader directory always must exist. The known mesh
and triangle SPIR-V variants are also copied when present. Each copied shader
must remain within the canonical source directory, be a regular file, be
20 bytes to 16 MiB with four-byte alignment and the SPIR-V magic. This checks
transport structure, not shader compilation or GPU behavior. Unrelated SDK,
compiler, source and developer files are not included.

The explicit `licenseRoot` identifies the engine source checkout. The package
copies its exact engine `LICENSE` and pinned EnTT, GLFW, picojson and stb notices
to `licenses/<dependency>/`. Lua's complete embedded copyright, permission and
disclaimer block is extracted from the pinned `third_party/lua/lua.h` into
`licenses/lua/LICENSE.txt`; its source code is not redistributed by this helper.
Missing notices, escaping source links/junctions or copy failures prevent
publication. The engine root is never guessed from the authored project root.
Projects remain responsible for their own content/asset licenses.

Lua dependency indexing uses the restricted runtime's canonical candidate order:
script-directory `module.lua`, script-directory `module/init.lua`, project-root
`module.lua`, then project-root `module/init.lua`. Root-qualified names such as
`require('scripts.progression')` therefore package exactly the files used by
runtime playback, including nested `init.lua` modules. Dependency records retain
the declared stable asset IDs. Missing, unindexed or escaping modules fail.
The package still includes the manifest, startup scene, all indexed declared
assets and declared Lua module assets. `PACKAGE.txt` records runtime, shader and
notice status. `run.cmd` and `run.sh` derive an absolute directory from their own
location and pass absolute package-relative runtime, manifest and shader paths.
They forward caller arguments, permitting bounded `--headless --ticks N` checks
and preserving the runtime exit status. Windows disables inherited delayed
expansion and escapes percent signs in authored relative paths.

Destination/source overlap and existing destination are refused. The helper
creates an exclusive sibling staging directory, copies required files, closes
metadata/launcher streams, and renames the complete staging directory into place.
Supplemental failure removes only task-owned staging; no partial destination is
published. Supplemental output collisions with authored assets fail. This is an
export boundary for trusted host directories, not a sandbox against another
process racing filesystem entries.

The existing tooling test's package case covers compatible headless output,
shader autodiscovery/explicit sources, SPIR-V byte preservation, the shader
allowlist, notice contents and Lua extraction, absolute launcher arguments,
missing/invalid shaders, missing notices, real source junction escapes, and
absence of published partial directories or leftover staging. Build/run with
`make BUILD=build/packaging-check build/packaging-check/bin/WorkflowTests.exe`
on Windows (substitute installed `mingw32-make`), then run that executable.
A synthetic runtime and structurally valid SPIR-V fixtures establish packaging
behavior only. Launching the integrated real binary from a different working
directory and its real Vulkan playback are separate integration checks.
