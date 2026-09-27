# MCP stdio adapter

The engine's MCP adapter is a read-only transport over the shared project
service. Start it with `CPPGameEngine mcp <project-root>` and connect an MCP
client to the process using the standard input/output transport. The project
root is fixed for the lifetime of the process; tool paths are relative to it.

The adapter supports both current MCP `2026-07-28` and legacy handshake version
`2025-11-25`. Current clients use stateless `server/discover`, then include
`_meta.io.modelcontextprotocol/protocolVersion` and
`_meta.io.modelcontextprotocol/clientCapabilities` on every request. Results
include `resultType: "complete"`; protocol errors use the current error codes.
Legacy clients use `initialize`, `notifications/initialized`, then
`tools/list` and `tools/call`. Each input line is limited to 1 MiB, and tool
responses are capped at 1 MiB. An oversized tool result becomes a bounded MCP
tool error so the process still emits valid protocol JSON. Standard output
contains only compact JSON-RPC messages; diagnostic logging goes to standard
error. The adapter does not expose resources, prompts, server notifications,
or edit tools.

The available tools are:

| Tool | Required argument | Result |
| --- | --- | --- |
| `project_validate` | `manifest`: project-relative manifest path | Validation status and structured diagnostics |
| `scene_inspect` | `scene`: project-relative scene path | The shared service's structured scene representation, with catalog asset IDs resolved and validated |

Both tools use the same `project::validateProject` and
`project::inspectScene` operations as the engine's other interfaces. Their MCP
metadata marks them read-only and idempotent. Absolute paths, missing files,
and paths that resolve outside the opened project root (including through
symlinks) are rejected. Validation failures return `isError: true` with a
`structuredContent.diagnostics` array containing the service's diagnostic
codes and field paths. Scene inspection loads the opened project's
`project.json` catalog before inspecting, so stable script, mesh, and texture
IDs receive the same validation as project and editor operations. These are
tool results, distinct from JSON-RPC protocol errors such as malformed
requests or unknown methods.

The current protocol replaced the initialize handshake with per-request
metadata and `server/discover`; the adapter preserves the older handshake for
legacy clients. See the official
[stdio transport specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio),
[versioning specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning),
[discovery specification](https://modelcontextprotocol.io/specification/2026-07-28/server/discover),
and [tools specification](https://modelcontextprotocol.io/specification/2026-07-28/server/tools). The
legacy behavior follows the [2025-11-25 lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)
and [tools specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
