#ifndef PROTOCOL_DDP_H
#define PROTOCOL_DDP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise a UDP listener on the DDP port (4048).
 * Returns the lwIP udp_pcb pointer, or NULL on failure. */
void *protocol_ddp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_DDP_H */
