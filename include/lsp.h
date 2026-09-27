#ifndef VEXEL_LSP_H
#define VEXEL_LSP_H

#ifdef __cplusplus
extern "C" {
#endif

/* JSON-RPC (Content-Length framing) language server over stdio.
 * initialize, didOpen/didChange (full sync), hover, completion,
 * definition, documentSymbol, shutdown. Never exits on bad input. */
int vx_lsp_run(void);

#ifdef __cplusplus
}
#endif

#endif
