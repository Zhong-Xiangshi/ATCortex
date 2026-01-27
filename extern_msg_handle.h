#ifndef EXTERN_MSG_HANDLE_H
#define EXTERN_MSG_HANDLE_H
#include "include/ATCortex.h"

enum atc_result extern_msg_queue_init(struct atc_context *context);
void extern_msg_handle(struct atc_context *context);
#endif // EXTERN_MSG_HANDLE_H