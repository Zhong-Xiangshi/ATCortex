#include "interface.h"

struct atc_interface g_atc_interface = {0};

enum atc_result atc_interface_register(struct atc_interface *interface){
    if(!interface)
        return ATC_ERROR;
    if(!interface->atc_malloc || !interface->atc_free || !interface->atc_log || !interface->atc_send)
        return ATC_ERROR;
    if(!interface->atc_queue_create || !interface->atc_queue_send || !interface->atc_queue_recv)
        return ATC_ERROR;
    if(!interface->atc_semaphore_create_binary || !interface->atc_semaphore_take
        || !interface->atc_semaphore_give || !interface->atc_semaphore_delete
        || !interface->atc_semaphore_give_isr)
        return ATC_ERROR;
    if(!interface->atc_get_tick_ms)
        return ATC_ERROR;
    g_atc_interface = *interface;
    return ATC_SUCCESS;
}