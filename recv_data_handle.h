#ifndef RECV_DATA_HANDLE_H
#define RECV_DATA_HANDLE_H
#include "include/ATCortex.h"

enum atc_result recv_data_init(struct atc_context *context);
void recv_data_handle(struct atc_context *context);
void command_end_handle(struct atc_context *context, enum atc_result result);
void clear_response_buffer(struct atc_context *context);

#endif // RECV_DATA_HANDLE_H